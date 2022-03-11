local lfm = lfm

local fm = lfm.fm

local util = require("util")
local lfs = require("lfs")
local basename = util.basename
local dirname = util.dirname
local file_split = util.file_split

local M = {}


local function file_exists(path)
	return lfs.attributes(path) ~= nil
end

---Copy a string to the clipboard.
---@param text string
---@param primary boolean
local function wl_copy(text, primary)
	if primary then
		lfm.execute({"sh", "-c", 'printf -- "%s" "$*" | wl-copy --primary', "_", text}, {fork=true})
	else
		lfm.execute({"sh", "-c", 'printf -- "%s" "$*" | wl-copy', "_", text}, {fork=true})
	end
end

---Copy full paths of the current selection to the clipboard.
function M.yank_path()
	local files = lfm.sel_or_cur()
	if #files > 0 then
		wl_copy(table.concat(files, "\n"), true)
	end
end

---Copy filenames of the current selection to the clipboard.
function M.yank_name()
	local files = lfm.sel_or_cur()
	if #files > 0 then
		for i, file in pairs(files) do
			files[i] = basename(file)
		end
		wl_copy(table.concat(files, "\n"), true)
	end
end

---Rename (move) the currently selected file.
---@param name string
function M.rename(name)
	local file = fm.current_file()
	if file then
		if not file_exists(name) then
			lfm.execute({"mv", "--", file, name}, {fork=true})
		else
			lfm.error("file exists")
		end
	end
end

---Populate the prompt to rename the current file up to its extension.
function M.rename_until_ext()
	local file = basename(fm.current_file())
	if file then
		local _, ext = file_split(file)
		if not ext then
			lfm.cmd.line_set(":", "rename ", "")
		else
			lfm.cmd.line_set(":", "rename ", "."..ext)
		end
	end
end

---Populate the prompt to rename the current file just before its extension.
function M.rename_before_ext()
	local file = basename(fm.current_file())
	if file then
		local name, ext = file_split(file)
		if not ext then
			lfm.cmd.line_set(":", "rename " .. file, "")
		else
			lfm.cmd.line_set(":", "rename "..name, "."..ext)
		end
	end
end

---Populate the prompt to rename at the beginning of the file name.
function M.rename_before()
	lfm.cmd.line_set(":", "rename ", basename(fm.current_file()))
end

---Populate the prompt to rename at the end of the file name.
function M.rename_after()
	lfm.cmd.line_set(":", "rename " .. basename(fm.current_file()), "")
end

---Create absolute symbolic links of the current load at the current location.
---Aborts if the mode is "move" instead of "copy".
function M.symlink()
	local files, mode = fm.paste_buffer_get()
	if mode == "copy" then
		for _, f in pairs(files) do
			lfm.execute({"ln", "-s", "--", f}, {fork=true})
		end
	end
	fm.paste_buffer_set({})
end

---Create relative symbolic links of the current load at the current location.
---Aborts if the mode is "move" instead of "copy".
function M.symlink_relative()
	local files, mode = fm.paste_buffer_get()
	if mode == "copy" then
		for _, f in pairs(files) do
			lfm.execute({"ln", "-s", "--relative", "--", f}, {fork=true})
		end
	end
	fm.paste_buffer_set({})
end

---Go to the location pointed at by the symlink at the cursor position.
function M.follow_link()
	local file = fm.current_file()
	local target = lfm.shell.popen({"readlink", "--", file})[1]
	if target then
		-- TODO: do these things even work with spaces in filenames? (on 2022-02-12)
		lfm.eval("cd "..dirname(target))
		fm.sel(basename(file))
	end
end

-- TODO: this is useful, expose it somewhere (on 2022-03-11)
local function chain(f, args, opts)
	args = args or {}
	opts = opts or {}
	local co
	local resume = function(r) coroutine.resume(co, r) end
	co = coroutine.create(function()
		for _, c in ipairs(args) do
			f(c, resume)
			if coroutine.yield(co) ~= 0 and opts.errexit then
				return
			end
		end
	end)
	coroutine.resume(co)
end

---Paste the load in the current directory, making backups of existing files.
function M.paste()
	local files, mode = fm.paste_buffer_get()
	if #files == 0 then
		return
	end
	local pwd = lfm.fn.getpwd()
	--- spawning all these shells is fine with a sane amount of files
	local attributes = lfs.attributes
	local format = string.format
	local execute = lfm.execute
	chain(function(file, resume)
		local base = basename(file)
		local target = pwd.."/"..base
		local num = 1
		while attributes(target, "mode") do
			target = format("%s/%s.~%d~", pwd, base, num)
			num = num + 1
		end
		if mode == "move" then
			execute({"mv", "--", file, target}, {callback=resume, fork=true, out=false})
		else
			execute({"cp", "-r", "--", file, target}, {callback=resume, fork=true, out=false})
		end
	end,
	files, {errexit=true})
	fm.paste_buffer_set({})
end

---Toggle paste mode.
function M.paste_toggle()
	local mode = lfm.fm.paste_mode_get()
	lfm.fm.paste_mode_set(mode == "copy" and "move" or "copy")
end

---Paste the load in the current directory, overwriting existing files.
function M.paste_overwrite()
	local files, mode = fm.paste_buffer_get()
	if #files == 0 then
		return
	end
	-- this doesn't "move" on the same filesystem, it copies and deletes
	local cmd
	if mode == "move" then
		cmd = {"rsync", "-r", "--remove-source-files", "--", unpack(files)}
	elseif mode == "copy" then
		cmd = {"rsync", "-r", "--", unpack(files)}
	else
		-- not reached
	end
	table.insert(cmd, "./")
	lfm.execute(cmd, {fork=true})
	fm.paste_buffer_set({})
end

return M
