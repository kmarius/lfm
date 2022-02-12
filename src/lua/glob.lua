local lfm = lfm

local fm = lfm.fm
local ui = lfm.ui
local find = require("find").find

local M = {}

-- TODO: should probably escape some special chars (on 2022-02-12)

--Convert a glob to a lua pattern.
---@param glob string
---@return string pattern
function M.glob_to_pattern(glob)
	local res = string.gsub(glob, "%.", "%%.")
	res = string.gsub(res, "%*", ".*")
	res = string.gsub(res, "%?", ".?")
	return "^"..res.."$"
end

--Select all files in the current directory matching a glob.
---@param glob string
---@return string[] files
function M.glob_select(glob)
	local pat = M.glob_to_pattern(glob)
	local sel = {}
	for _, file in pairs(fm.current_dir().files) do
		if string.match(require("util").basename(file), pat) then
			table.insert(sel, file)
		end
	end
	lfm.log.debug(glob)
	fm.selection_set(sel)
end

--Recursiv select all files matching a glob in the current directory and subdirectories.
---(probably breaks on loops)
---@param glob string
---@return string[] files
function M.glob_select_recursive(glob)
	local pat = M.glob_to_pattern(glob)
	local sel = {}
	local function filter(f) return string.match(f, pat) end
	for f in find(os.getenv("PWD"), filter) do
		table.insert(sel, f)
	end
	fm.selection_set(sel)
end

M.mode_glob_select = {
	prefix = "glob-select: ",
	enter = lfm.cmd.clear,
	esc = function() lfm.cmd.clear() fm.selection_clear() end,
	change = function() M.glob_select(lfm.cmd.getline()) ui.draw() end,
}

return M
