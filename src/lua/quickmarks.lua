local lfm = lfm
local eval = lfm.eval
local clear = lfm.cmd.clear
local line_get = lfm.cmd.line_get
local map = lfm.map
local open = io.open
local getenv = os.getenv

-- TODO: there is also marks in fm.c, those should probably be removed, '' could
-- be handled with hooks (on 2022-02-12)

-- TODO: add mark-delete (on 2022-07-31)

local path = lfm.config.user_datadir .. "/quickmarks.lua"

local marks = {}

---@alias char string
---@alias path string

---Add a quickmark (essentially just setting a keybind).
---@param m char
---@param loc? path
local function mark_add(m, loc)
	if loc then
		local cmd = "cd "..loc
		map("'"..m, function() eval(cmd) end, {desc=cmd})
		marks[m] = loc
	end
end

local function load()
	local f = loadfile(path)
	if f then
		for m, loc in pairs(f()) do
			mark_add(m, loc)
		end
	end
end

local function save()
	local file = open(path, "w")
	if file then
		file:write("return {\n")
		for m, loc in pairs(marks) do
			file:write(string.format('\t["%s"] = "%s",\n', m, loc))
		end
		file:write("}\n")
		file:close()
	end
end

---Add a quickmark fork the current directory with key `m`.
---@param m char
local function mark_save(m)
	load()
	mark_add(m, getenv("PWD"))
	save()
end

---@class setup_opts
---@field quickmarks table<char, path>

---Set up quickmarks.
---@param t setup_opts
local function setup(t)
	t = t or {}
	load()
	for k, v in pairs(t.quickmarks or {}) do
		mark_add(k, v)
	end
	save()
end

local mode_mark_save = {
	prefix = "mark-save: ",
	on_enter = clear,
	on_esc = clear,
	on_change = function()
		mark_save(line_get())
		clear()
	end,
}

return {
	mark_add = mark_add,
	mark_save = mark_save,
	setup = setup,
	mode_mark_save = mode_mark_save
}
