local quickmarks = {}

---@alias Lfm.Char string
---@alias Lfm.Path string

local lfm = lfm
local eval = lfm.eval
local map = lfm.map
local getpwd = lfm.fn.getpwd
local cmd_clear = lfm.cmd.clear
local line_get = lfm.cmd.line_get

local open = io.open

local marks_path = lfm.config.statedir .. "/quickmarks.lua"

local marks = {}

---Add a quickmark (essentially just setting a keybind).
---@param m Lfm.Char
---@param loc Lfm.Path
local function mark_set(m, loc)
	loc = loc or getpwd()
	local cmd = "cd " .. loc
	map("'" .. m, function()
		eval(cmd)
	end, { desc = cmd })
	marks[m] = loc
end

---Loads marks from disk, overwriting those currently set.
local function load_from_file()
	local f = loadfile(marks_path)
	if f then
		for m, loc in pairs(f()) do
			mark_set(m, loc)
		end
	end
end

local escape = {
	['"'] = '\\"',
	["\\"] = "\\\\",
}

---Writes currently set quickmarks to disk.
local function write_to_file()
	local file = open(marks_path, "w")
	if file then
		file:write("return {\n")
		for m, loc in pairs(marks) do
			m = escape[m] or m
			file:write(string.format('\t["%s"] = "%s",\n', m, loc))
		end
		file:write("}\n")
		file:close()
	end
end

---Add a quick mark for the current directory with character `m`.
---@param m Lfm.Char
function quickmarks.save(m)
	load_from_file()
	mark_set(m, getpwd())
	write_to_file()
end

---Add multiple quick marks. Loads quick marks from disk, sets the marks passed
---in `t` and writes them back.
---@param t table<Lfm.Char, Lfm.Path>
function quickmarks.add(t)
	t = t or {}
	load_from_file()
	for k, v in pairs(t) do
		mark_set(k, v)
	end
	write_to_file()
end

---Deletes the mark for character `m` and persists the change to disk.
---@param m Lfm.Char
function quickmarks.delete(m)
	if marks[m] then
		load_from_file()
		map("'" .. m, nil)
		marks[m] = nil
		write_to_file()
	end
end

local mode_mark_save = {
	name = "mark-save",
	input = true,
	prefix = "mark-save: ",
	on_return = function()
		cmd_clear()
		lfm.mode("normal")
	end,
	on_esc = cmd_clear,
	on_change = function()
		quickmarks.save(line_get())
		cmd_clear()
		lfm.mode("normal")
	end,
}

local mode_mark_delete = {
	name = "mark-delete",
	input = true,
	prefix = "mark-delete: ",
	on_return = function()
		cmd_clear()
		lfm.mode("normal")
	end,
	on_esc = cmd_clear,
	on_change = function()
		quickmarks.delete(line_get())
		cmd_clear()
		lfm.mode("normal")
	end,
}

function quickmarks._setup()
	lfm.register_command("mark-save", quickmarks.mark_save)
	lfm.register_command("mark-delete", quickmarks.mark_delete)

	lfm.register_mode(mode_mark_save)
	lfm.register_mode(mode_mark_delete)

	lfm.map("m", function()
		lfm.mode(mode_mark_save.name)
	end, { desc = "save quickmark" })

	lfm.map("dm", function()
		lfm.mode(mode_mark_delete.name)
	end, { desc = "delete quickmark" })
end

return quickmarks
