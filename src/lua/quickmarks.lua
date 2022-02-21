local lfm = lfm
local eval = lfm.eval
local clear = lfm.cmd.clear
local getline = lfm.cmd.getline

-- TODO: there is also marks in fm.c, those should probably be removed, '' could
-- be handled with hooks (on 2022-02-12)

local M = {}

---@alias char string
---@alias path string

---Add a quickmark (essentially just setting a keybind).
---@param m char
---@param loc path
function M.mark_add(m, loc)
	local cmd = "cd "..loc
	lfm.map("'"..m, function() eval(cmd) end, {desc=cmd})
end

---Add a quickmark fork the current directory with key `m`.
---@param m char
function M.mark_save(m)
	M.mark_add(m, os.getenv("PWD"))
end

---@class setup_opts
---@field quickmarks table<char, path>

---Set up quickmarks.
---@param t setup_opts
function M.setup(t)
	t = t or {}
	for k, v in pairs(t.quickmarks or {}) do
		M.mark_add(k, v)
	end
end

M.mode_mark_save = {
	prefix = "mark-save: ",
	on_enter = clear,
	on_esc = clear,
	on_change = function()
		M.mark_save(getline())
		clear()
	end,
}

return M
