local lfm = lfm

-- TODO: there is also marks in fm.c, those should probably be removed, '' could
-- be handled with hooks (on 2022-02-12)

local M = {}

---@alias char string
---@alias path string

---@param m char
---@param loc path
function M.mark_add(m, loc)
	lfm.map("'"..m, function() lfm.eval("cd ".. loc) end, {desc=loc})
end

---Add a quickmark to the current directory with key `m`.
---@param m char
function M.mark_save(m)
	local loc = os.getenv("PWD")
	lfm.map("'"..m, function() lfm.eval("cd ".. loc) end, {desc=loc})
	M.mark_add(m, loc)
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

---@class Setup
---@field quickmarks table<char, path>

---Setup quickmarks.
---@param t Setup
function M.setup(t)
	t = t or {}
	for k, v in pairs(t.quickmarks or {}) do
		M.mark_add(k, v)
	end
end

return M
