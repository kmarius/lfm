local M = { _NAME = ... }

local lfm = lfm

local fm = lfm.fm

local util = require("lfm.util")
local stat = require("posix.sys.stat")

local basename = util.basename
local dirname = util.dirname
local file_split = util.file_split

local function file_exists(path)
	return stat.stat(path) ~= nil
end

---Copy a string to the clipboard.
---```lua
---    M.wl_copy("some text")
---```
---```lua
---    -- Use primary clipboard
---    M.wl_copy("some text", true)
---```
---@param text string|string[]
---@param primary boolean
local function wl_copy(text, primary)
	if primary then
		lfm.spawn({ "wl-copy", "-n", "--primary" }, { stdin = text })
	else
		lfm.spawn({ "wl-copy", "-n" }, { stdin = text })
	end
end

---Copy full paths of the current selection to the clipboard.
---```lua
---    M.yank_path()
---```
function M.yank_path()
	local files = lfm.sel_or_cur()
	if #files > 0 then
		wl_copy(files, true)
	end
end

---Copy filenames of the current selection to the clipboard.
---```lua
---    M.yank_name()
---```
function M.yank_name()
	local files = lfm.sel_or_cur()
	if #files > 0 then
		for i, file in pairs(files) do
			files[i] = basename(file)
		end
		wl_copy(files, true)
	end
end

---Rename (move) the currently selected file.
---```lua
---    M.rename("file.txt")
---```
---@param name string
function M.rename(name)
	local file = fm.current_file()
	if file then
		if not file_exists(name) then
			lfm.spawn({ "mv", "--", file, name })
		else
			lfm.error("file exists")
		end
	end
end

---Populate the prompt to rename the current file up to its extension.
---```lua
---    M.rename_until_ext()
---```
function M.rename_until_ext()
	local file = basename(fm.current_file())
	if file then
		local _, ext = file_split(file)
		if not ext then
			lfm.mode("command")
			lfm.cmd.line_set("rename ", "")
		else
			lfm.mode("command")
			lfm.cmd.line_set("rename ", "." .. ext)
		end
	end
end

---Populate the prompt to rename the current file just before its extension.
---```lua
---    M.rename_before_ext()
---```
function M.rename_before_ext()
	local file = basename(fm.current_file())
	if file then
		local name, ext = file_split(file)
		if not ext then
			lfm.mode("command")
			lfm.cmd.line_set("rename " .. file, "")
		else
			lfm.mode("command")
			lfm.cmd.line_set("rename " .. name, "." .. ext)
		end
	end
end

---Populate the prompt to rename at the beginning of the file name.
---```lua
---    M.rename_before()
---```
function M.rename_before()
	lfm.mode("command")
	lfm.cmd.line_set("rename ", basename(fm.current_file()))
end

---Populate the prompt to rename at the end of the file name.
---```lua
---    M.rename_after()
---```
function M.rename_after()
	lfm.mode("command")
	lfm.cmd.line_set("rename " .. basename(fm.current_file()), "")
end

---Create absolute symbolic links of the current load at the current location.
---Aborts if the mode is "move" instead of "copy".
---```lua
---    M.symlink()
---```
function M.symlink()
	local files, mode = fm.paste_buffer_get()
	if mode == "copy" then
		for _, f in pairs(files) do
			lfm.spawn({ "ln", "-s", "--", f })
		end
	end
	fm.paste_buffer_set({})
end

---Create relative symbolic links of the current load at the current location.
---Aborts if the mode is "move" instead of "copy".
---```lua
---    M.symlink_relative()
---```
function M.symlink_relative()
	local files, mode = fm.paste_buffer_get()
	if mode == "copy" then
		for _, f in pairs(files) do
			lfm.spawn({ "ln", "-s", "--relative", "--", f })
		end
	end
	fm.paste_buffer_set({})
end

---Go to the location pointed at by the symlink at the cursor position.
---```lua
---    M.follow_link()
---```
function M.follow_link()
	local file = fm.current_file()
	local target = lfm.shell.popen({ "readlink", "--", file })[1]
	if target then
		-- TODO: do these things even work with spaces in filenames? (on 2022-02-12)
		lfm.eval("cd " .. dirname(target))
		fm.sel(basename(target))
	end
end

-- TODO: this is useful, expose it somewhere (on 2022-03-11)
local function chain(f, args, opts)
	args = args or {}
	opts = opts or {}
	local co
	local callback = opts.callback
	opts.callback = function(r)
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
					if callback then
						callback(ret)
					end
					return
				end
			end
		end
		if callback then
			callback(ret)
		end
	end)
	coroutine.resume(co)
end

-- TODO: make a s mall module for ansi colors or put it in colors.lua
local c27 = string.char(27)
local green = c27 .. "[32m"
local clear = c27 .. "[0m"

---Paste the load in the current directory, making backups of existing files.
---```lua
---    M.paste()
---```
function M.paste()
	local files, mode = fm.paste_buffer_get()
	if #files == 0 then
		return
	end
	local pwd = lfm.fn.getpwd()
	--- spawning all these shells is fine with a sane amount of files
	local stat_stat = stat.stat
	local format = string.format
	local reload_dirs = { [pwd] = true }
	if mode == "move" then
		for _, file in ipairs(files) do
			reload_dirs[dirname(file)] = true
		end
	end
	local cb = function(ret)
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
		local base = basename(file)
		local target = pwd .. "/" .. base
		local num = 1
		while stat_stat(target) do
			target = format("%s/%s.~%d~", pwd, base, num)
			num = num + 1
		end
		if mode == "move" then
			return { "mv", "--", file, target }
		else
			return { "cp", "-r", "--", file, target }
		end
	end, files, { errexit = true, out = false, callback = cb })
	fm.paste_buffer_set({})
end

---Toggle paste mode from "copy" to "move" and reverse.
---```lua
---    M.toggle_paste()
---```
function M.toggle_paste()
	local mode = lfm.fm.paste_mode_get()
	lfm.fm.paste_mode_set(mode == "copy" and "move" or "copy")
end

---Paste the load in the current directory, overwriting existing files.
---```lua
---    M.paste_overwrite()
---```
function M.paste_overwrite()
	local files, mode = fm.paste_buffer_get()
	if #files == 0 then
		return
	end
	local reload_dirs = { [lfm.fn.getpwd()] = true }
	if mode == "move" then
		for _, file in ipairs(files) do
			reload_dirs[dirname(file)] = true
		end
	end
	local cb = function(ret)
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
	lfm.spawn(cmd, { callback = cb })
	fm.paste_buffer_set({})
end

return M
