local M = { _NAME = ... }

local lfm = lfm

local api = lfm.api
local fm = lfm.fm
local util = lfm.util

local stat = require("posix.sys.stat")
local unistd = require("posix.unistd")
local fs = require("lfm.fs")

---
---Copy a string to the clipboard.
---
---```lua
---  M.wl_copy("some text")
---```
---```lua
---  -- Use primary clipboard
---  M.wl_copy("some text", true)
---```
---
---@param text string|string[]
---@param primary boolean
local function wl_copy(text, primary)
	local cmd = { "wl-copy", "-n" }
	if primary then
		table.insert(cmd, "--primary")
	end
	lfm.spawn(cmd, { stdin = text, on_stderr = true })
end

---
---Copy full paths of the current selection to the clipboard.
---
---Example:
---```lua
---  M.yank_path()
---```
---
function M.yank_path()
	local files = util.selection()
	if #files > 0 then
		wl_copy(files, true)
	end
end

---
---Copy filenames of the current selection to the clipboard.
---
---Example:
---```lua
---  M.yank_name()
---```
---
function M.yank_name()
	local files = util.selection()
	if #files > 0 then
		for i, file in pairs(files) do
			files[i] = fs.basename(file)
		end
		wl_copy(files, true)
	end
end

---
---Rename (move) the currently selected file.
---
---Example:
---```lua
---  M.rename("file.txt")
---```
---
---@param name string
function M.rename(name)
	local file = fm.current_file()
	if file then
		if fs.exists(name) then
			error("file exists")
		end
		lfm.spawn({ "mv", "--", file, name }, { on_stderr = true })
	end
end

---
---Populate the prompt to rename the current file up to its extension.
---
---Example:
---```lua
---  M.rename_until_ext()
---```
function M.rename_until_ext()
	local file = fs.basename(fm.current_file())
	if file then
		local _, ext = fs.split_ext(file)
		api.mode("command")
		api.cmdline_line_set("rename ", ext)
	end
end

---
---Populate the prompt to rename the current file just before its extension.
---
---Example:
---```lua
---  M.rename_before_ext()
---```
---
function M.rename_before_ext()
	local file = fs.basename(fm.current_file())
	if file then
		local stem, ext = fs.split_ext(file)
		api.mode("command")
		api.cmdline_line_set("rename " .. stem, ext)
	end
end

---
---Populate the prompt to rename at the beginning of the file name.
---
---Example:
---```lua
---  M.rename_before()
---```
---
function M.rename_before()
	api.mode("command")
	api.cmdline_line_set("rename ", fs.basename(fm.current_file()))
end

---
---Populate the prompt to rename at the end of the file name.
---
---Example:
---```lua
---  M.rename_after()
---```
---
function M.rename_after()
	api.mode("command")
	api.cmdline_line_set("rename " .. fs.basename(fm.current_file()), "")
end

---
---Create absolute symbolic links of the current load at the current location.
---Aborts if the mode is "move" instead of "copy".
---
---Example:
---```lua
---  M.symlink()
---```
---
function M.symlink()
	local files, mode = fm.get_paste_buffer()
	if mode == "copy" then
		for _, f in pairs(files) do
			lfm.spawn({ "ln", "-s", "--", f }, { on_stderr = true })
		end
	end
	fm.set_paste_buffer({})
end

---
---Create relative symbolic links of the current load at the current location.
---Aborts if the mode is "move" instead of "copy".
---
---Example:
---```lua
---  M.symlink_relative()
---```
---
function M.symlink_relative()
	local files, mode = fm.get_paste_buffer()
	if mode == "copy" then
		for _, f in pairs(files) do
			lfm.spawn({ "ln", "-s", "--relative", "--", f }, { on_stderr = true })
		end
	end
	fm.set_paste_buffer({})
end

---
---Go to the location pointed at by the symlink at the cursor position.
---
---Example:
---```lua
---  M.follow_link()
---```
---
function M.follow_link()
	local file = fm.current_file()
	local target, err = unistd.readlink(file)
	if err then
		error(err)
	end
	fm.chdir(fs.dirname(target), true)
	fm.select(fs.basename(target) --[[@as string]])
end

-- TODO: this is useful, expose it somewhere (on 2022-03-11)
local function chain(f, args, opts)
	args = args or {}
	opts = opts or {}
	local co
	local on_exit = opts.on_exit
	opts.on_exit = function(r)
		coroutine.resume(co, r)
	end
	co = coroutine.create(function()
		local ret = 0
		for _, arg in ipairs(args) do
			local cmd = f(arg)
			if cmd then
				lfm.spawn(cmd, opts)
				ret = coroutine.yield(co)
				if ret ~= 0 and opts.errexit then
					if on_exit then
						on_exit(ret)
					end
					return
				end
			end
		end
		if on_exit then
			on_exit(ret)
		end
	end)
	coroutine.resume(co)
end

-- TODO: make a s mall module for ansi colors or put it in colors.lua
local c27 = string.char(27)
local green = c27 .. "[32m"
local clear = c27 .. "[0m"

---
---Paste the load in the current directory, making backups of existing files.
---
---Example:
---```lua
---  M.paste()
---```
---
function M.paste()
	local files, mode = fm.get_paste_buffer()
	if #files == 0 then
		return
	end
	local pwd = lfm.fn.getpwd()
	--- spawning all these shells is fine with a sane amount of files
	local reload_dirs = { [pwd] = true }
	if mode == "move" then
		for _, file in ipairs(files) do
			reload_dirs[fs.dirname(file)] = true
		end
	end
	local on_exit = function(ret)
		for dir, _ in pairs(reload_dirs) do
			fm.load(dir)
		end
		if ret ~= 0 then
			return
		end
		local operation = mode == "move" and "moving" or "copying"
		local msg =
			string.format("%sfinished %s %d %s%s", green, operation, #files, #files == 1 and "file" or "files", clear)
		print(msg)
	end
	chain(function(file)
		local base = fs.basename(file)
		local target = pwd .. "/" .. base
		local num = 1
		while stat.stat(target) do
			target = string.format("%s/%s.~%d~", pwd, base, num)
			num = num + 1
		end
		if mode == "move" then
			return { "mv", "--", file, target }
		else
			return { "cp", "-r", "--", file, target }
		end
	end, files, { errexit = true, on_stderr = true, on_exit = on_exit })
	fm.set_paste_buffer({})
end

---
---Toggle paste mode from "copy" to "move" and reverse.
---
---Example:
---```lua
---  M.toggle_paste()
---```
---
function M.toggle_paste()
	local mode = fm.get_paste_mode()
	fm.set_paste_mode(mode == "copy" and "move" or "copy")
end

---
---Paste the load in the current directory, overwriting existing files.
---
---Example:
---```lua
---  M.paste_overwrite()
---```
---
function M.paste_overwrite()
	local files, mode = fm.get_paste_buffer()
	if #files == 0 then
		return
	end
	local reload_dirs = { [lfm.fn.getpwd()] = true }
	if mode == "move" then
		for _, file in ipairs(files) do
			reload_dirs[fs.dirname(file)] = true
		end
	end
	local on_exit = function(ret)
		for dir, _ in pairs(reload_dirs) do
			fm.load(dir)
		end
		if ret ~= 0 then
			return
		end
		local operation = mode == "move" and "moving" or "copying"
		local msg =
			string.format("%sfinished %s %d %s%s", green, operation, #files, #files == 1 and "file" or "files", clear)
		print(msg)
	end
	-- this doesn't "move" on the same filesystem, it copies and deletes
	local cmd
	if mode == "move" then
		cmd = { "rsync", "-r", "--remove-source-files", "--", unpack(files) }
	elseif mode == "copy" then
		cmd = { "rsync", "-r", "--", unpack(files) }
	else
		-- not reached
	end
	table.insert(cmd, "./")
	lfm.spawn(cmd, { on_exit = on_exit, on_stderr = true })
	fm.set_paste_buffer({})
end

return M
