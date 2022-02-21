local lfm = lfm
local cmd = lfm.cmd

local M = {}

local file = nil

local function mode_esc()
	lfm.nohighlight()
	if file then
		lfm.fm.sel(require("util").basename(file))
	end
end

local function mode_enter()
	lfm.search_next(true)
end

local function mode_change()
	lfm.search(cmd.getline())
	lfm.search_next(true)
end

local function mode_back_change()
	lfm.search_back(cmd.getline())
	lfm.search_next(true)
end

M.mode_search = {
	prefix = "/",
	on_enter = mode_enter,
	on_esc = mode_esc,
	on_change = mode_change,
}

M.mode_search_back = {
	prefix = "?",
	on_enter = mode_enter,
	on_esc = mode_esc,
	on_change = mode_back_change,
}

function M.enter_mode()
	cmd.setprefix(M.mode_search.prefix)
	lfm.nohighlight()
	file = lfm.fm.current_file()
end

function M.enter_mode_back()
	cmd.setprefix(M.mode_search_back.prefix)
	lfm.nohighlight()
	file = lfm.fm.current_file()
end

return M
