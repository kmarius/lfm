local lfm = lfm
local cmd = lfm.cmd

local M = {}

local file = nil

M.mode_search = {
	name = "search",
	input = true,
	prefix = "/",
	on_enter = function()
		lfm.nohighlight()
		file = lfm.fm.current_file()
	end,
	on_change = function()
		lfm.search(cmd.line_get())
		lfm.search_next(true)
	end,
	on_return = function()
		lfm.search_next(true)
		cmd.clear()
		lfm.mode("normal")
	end,
	on_esc = function()
		lfm.nohighlight()
		if file then
			lfm.fm.sel(require("lfm.util").basename(file))
		end
	end,
}

M.mode_search_back = {
	name = "search-back",
	input = true,
	prefix = "?",
	on_enter = function()
		lfm.nohighlight()
		file = lfm.fm.current_file()
	end,
	on_change = function()
		lfm.search_back(cmd.line_get())
		lfm.search_next(true)
	end,
	on_return = function()
		lfm.search_next(true)
		cmd.clear()
		lfm.mode("normal")
	end,
	on_esc = function()
		lfm.nohighlight()
		if file then
			lfm.fm.sel(require("lfm.util").basename(file))
		end
	end,
}

return M
