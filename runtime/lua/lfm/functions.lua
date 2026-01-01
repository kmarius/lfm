local M = { _NAME = ... }

local lfm = lfm

local api = lfm.api

local stat = require("posix.sys.stat")
local unistd = require("posix.unistd")
local fs = require("lfm.fs")

local function file_exists(path)
	return stat.stat(path) ~= nil
end

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
	if primary then
		lfm.spawn({ "wl-copy", "-n", "--primary" }, { stdin = text, on_stderr = true })
	else
		lfm.spawn({ "wl-copy", "-n" }, { stdin = text, on_stderr = true })
	end
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
	local files = api.fm_sel_or_cur()
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
	local files = api.fm_sel_or_cur()
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
	local file = api.current_file()
	if file then
		if not file_exists(name) then
			lfm.spawn({ "mv", "--", file, name }, { on_stderr = true })
		else
			lfm.error("file exists")
		end
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
	local file = fs.basename(api.current_file())
	if file then
		local _, ext = fs.split_ext(file)
		if not ext then
			lfm.mode("command")
			lfm.api.cmdline_line_set("rename ", "")
		else
			lfm.mode("command")
			lfm.api.cmdline_line_set("rename ", "." .. ext)
		end
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
	local file = fs.basename(api.current_file())
	if file then
		local name, ext = fs.split_ext(file)
		if not ext then
			lfm.mode("command")
			lfm.api.cmdline_line_set("rename " .. file, "")
		else
			lfm.mode("command")
			lfm.api.cmdline_line_set("rename " .. name, "." .. ext)
		end
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
	lfm.mode("command")
	api.cmdline_line_set("rename ", fs.basename(api.current_file()))
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
	lfm.mode("command")
	api.cmdline_line_set("rename " .. fs.basename(api.current_file()), "")
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
	local files, mode = api.fm_paste_buffer_get()
	if mode == "copy" then
		for _, f in pairs(files) do
			lfm.spawn({ "ln", "-s", "--", f }, { on_stderr = true })
		end
	end
	api.fm_paste_buffer_set({})
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
	local files, mode = api.fm_paste_buffer_get()
	if mode == "copy" then
		for _, f in pairs(files) do
			lfm.spawn({ "ln", "-s", "--relative", "--", f }, { on_stderr = true })
		end
	end
	api.fm_paste_buffer_set({})
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
	local file = api.current_file()
	local target, err = unistd.readlink(file)
	if err then
		error(err)
	end
	api.chdir(fs.dirname(target), true)
	api.fm_sel(fs.basename(target) --[[@as string]])
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
	local files, mode = api.fm_paste_buffer_get()
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
			api.fm_load(dir)
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
	api.fm_paste_buffer_set({})
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
	local mode = lfm.api.fm_paste_mode_get()
	lfm.api.fm_paste_mode_set(mode == "copy" and "move" or "copy")
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
	local files, mode = api.fm_paste_buffer_get()
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
			api.fm_load(dir)
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
	api.fm_paste_buffer_set({})
end

return M
