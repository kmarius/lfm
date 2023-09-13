local cmd = lfm.cmd
local map = lfm.map
local fm = lfm.fm
local compl = require("lfm.compl")
local util = require("lfm.util")

local a = util.a

local M = {}

function M._setup()
	-- COMMAND mode
	do
		local mode = {
			name = "command",
			input = true,
			prefix = ":",
			on_return = function(line)
				lfm.mode("normal")
				cmd.history_append(":", line)
				lfm.eval(line)
			end,
			on_change = compl.reset,
		}
		lfm.register_mode(mode)
		map(":", a(lfm.mode, "command"), { desc = "enter COMMAND mode" })
		map("<Up>", function()
			local line = cmd.history_prev()
			if line then
				cmd.line_set(line)
			end
		end, { mode = "command", desc = "Previous history item" })
		map("<Down>", function()
			local line = cmd.history_next()
			if line then
				cmd.line_set(line)
			end
		end, { mode = "command", desc = "Next history item" })
	end

	-- FILTER mode
	do
		local mode = {
			name = "filter",
			input = true,
			prefix = "filter: ",
			on_enter = function()
				cmd.line_set(fm.getfilter())
			end,
			on_change = function()
				fm.filter(cmd.line_get())
			end,
			on_return = function()
				lfm.mode("normal")
			end,
			on_esc = function()
				fm.filter("")
			end,
		}
		lfm.register_mode(mode)
		map("zf", a(lfm.mode, "filter"), { desc = "Enter FILTER mode" })
		map("zF", a(lfm.feedkeys, "zf<esc>"), { desc = "Remove current filter" })
	end

	-- TRAVEL mode
	do
		local mode = {
			name = "travel",
			input = true,
			prefix = "travel: ",
			on_return = function()
				local file = fm.current_file()
				if file then
					fm.filter("")
					if fm.open() then
						lfm.mode("normal")
						lfm.eval("open")
					else
						cmd.clear()
					end
				end
			end,
			on_esc = function()
				fm.filter("")
			end,
			on_change = function()
				fm.filter(cmd.line_get())
			end,
		}
		lfm.register_mode(mode)
		map("f", a(lfm.mode, "travel"), { desc = "Enter TRAVEL mode" })
		map("<c-n>", fm.down, { mode = "travel" })
		map("<c-p>", fm.up, { mode = "travel" })
		map("<Up>", fm.up, { mode = "travel" })
		map("<Down>", fm.down, { mode = "travel" })
		map("<a-h>", function()
			fm.filter("")
			fm.updir()
			cmd.clear()
		end, { mode = "travel", desc = "Move to parent directory" })
	end

	-- DELETE mode
	do
		local has_trash = os.execute("command -v trash-put >/dev/null") == 0

		local mode = {
			name = "delete",
			input = true,
			prefix = "delete [y/N]: ",
			on_return = function()
				lfm.mode("normal")
			end,
			on_change = function()
				local line = cmd.line_get()
				lfm.mode("normal")
				if line == "y" then
					-- TODO: use -- again when an updated version of trash-cli lands
					-- lfm.spawn({ "trash-put", "--", unpack(lfm.sel_or_cur()) })
					lfm.spawn({ "trash-put", unpack(lfm.sel_or_cur()) })
					fm.selection_set()
				end
			end,
		}
		lfm.register_mode(mode)
		if has_trash then
			map("df", a(lfm.mode, "delete"), { desc = "Trash file/selection" })
		end
	end

	-- SEARCH mode
	do
		local file = nil

		local mode = {
			name = "search",
			input = true,
			prefix = "/",
			on_enter = function()
				lfm.nohighlight()
				file = fm.current_file()
			end,
			on_change = function()
				lfm.search(cmd.line_get())
				lfm.search_next(true)
			end,
			on_return = function()
				lfm.search_next(true)
				lfm.mode("normal")
			end,
			on_esc = function()
				lfm.nohighlight()
				if file then
					fm.sel(util.basename(file))
				end
			end,
		}

		local mode_back = {
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
				lfm.mode("normal")
			end,
			on_esc = function()
				lfm.nohighlight()
				if file then
					lfm.fm.sel(require("lfm.util").basename(file))
				end
			end,
		}
		lfm.register_mode(mode)
		lfm.register_mode(mode_back)

		map("/", a(lfm.mode, "search"), { desc = "Search" })
		map("?", a(lfm.mode, "search-back"), { desc = "Search backwards" })
		map("n", lfm.search_next, { desc = "Go to next search result" })
		map("N", lfm.search_prev, { desc = "Go to previous search result" })
	end
end

return M
