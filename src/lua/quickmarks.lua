local lfm = lfm
local eval = lfm.eval

-- TODO: there is also marks in fm.c, those should probably be removed, '' could
-- be handled with hooks (on 2022-02-12)

local M = {}

---@alias char string
---@alias path string

---Add a quickmark (essentially just setting a keybind).
---@param m char
---@param loc path
function M.mark_add(m, loc)
	lfm.map("'"..m, function() eval("cd ".. loc) end, {desc=loc})
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
	enter = function() lfm.cmd.clear() end,
	esc = function() lfm.cmd.clear() end,
	change = function()
		local line = lfm.cmd.getline()
		lfm.log.debug(line)
		lfm.cmd.clear()
		M.mark_save(line)
	end,
}

return M
