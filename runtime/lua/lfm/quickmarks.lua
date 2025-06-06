local M = { _NAME = ... }

local lfm = lfm

---@alias Char string
---@alias Path string

local fn = lfm.fn

---Path to quickmarks file
M.path = lfm.paths.state_dir .. "/quickmarks.lua"

local marks = {}

---
---Add a quickmark (essentially just setting a keybind).
---
---Example:
---```lua
---  lfm.quickmarks.mark_set("h", "/home/john")
---  lfm.quickmarks.mark_set("a", lfm.fn.getpwd())
---  lfm.quickmarks.mark_set("a") -- same as above
---```
---
---@param char Char
---@param loc? Path Defaults to the current location
local function mark_set(char, loc)
	if #char == 0 then
		lfm.log.errorf("ignoring quickmark without key")
	end
	loc = loc or fn.getpwd()
	local cmd = "cd " .. loc
	lfm.map("'" .. char, function()
		lfm.eval(cmd)
	end, { desc = cmd })
	marks[char] = loc
end

---Loads marks from disk, overwriting those currently set.
local function load_from_file()
	local f = loadfile(M.path)
	if f then
		for char, loc in pairs(f()) do
			mark_set(char, loc)
		end
	end
end

local escape = {
	['"'] = '\\"',
	["\\"] = "\\\\",
}

---Writes currently set quickmarks to disk.
local function write_to_file()
	local file = assert(io.open(M.path, "w"))
	file:write("return {\n")
	for char, loc in pairs(marks) do
		char = escape[char] or char
		file:write(string.format('\t["%s"] = "%s",\n', char, loc))
	end
	file:write("}\n")
	file:close()
end

---
---Add a quick mark for the current directory with character `m`.
---
---Example:
---```lua
---  lfm.quickmarks.save("a")
---```
---
---@param char Char
function M.save(char)
	load_from_file()
	mark_set(char, fn.getpwd())
	write_to_file()
end

---
---Add multiple quick marks. Loads quick marks from disk, sets the marks passed
---in `t` and writes them back.
---
---Example:
---```lua
---  lfm.quickmarks.add({
---    a = "/home/john",
---    t = "/tmp",
---  })
---```
---
---@param t table<Char, Path>
function M.add(t)
	t = t or {}
	load_from_file()
	for char, loc in pairs(t) do
		mark_set(char, loc)
	end
	write_to_file()
end

---
---Deletes the mark for character `m` and persists the change to disk.
---
---Example:
---```lua
---  lfm.quickmarks.delete("a")
---```
---
---@param char Char
function M.delete(char)
	if marks[char] then
		load_from_file()
		lfm.map("'" .. char, nil)
		marks[char] = nil
		write_to_file()
	end
end

---Prompt to save a quickmark of the current location
function M.prompt_save()
	lfm.util.input({ prompt = "mark-save: ", single_key = true }, function(char)
		if char and #char > 0 then
			M.save(char)
		end
	end)
end

---Prompt to delete a quickmark
function M.prompt_delete()
	local lines = {}
	for char, loc in pairs(marks) do
		table.insert(lines, char .. "\t" .. loc)
	end
	lfm.api.ui_menu(lines)
	lfm.util.input({ prompt = "mark-delete: ", single_key = true }, function(char)
		if char and #char > 0 then
			M.delete(char)
		end
	end)
end

function M._setup(opts)
	lfm.validate("opts", opts, "table", true)
	opts = opts or {}

	lfm.map("m", M.prompt_save, { desc = "Save quickmark" })
	lfm.map("dm", M.prompt_delete, { desc = "Save quickmark" })
end

return M
