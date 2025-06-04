local M = { _NAME = ... }

local lfm = lfm

local options = lfm.o
local api = lfm.api
local fs = lfm.fs
local compl = require("lfm.compl")
local util = require("lfm.util")

local a = util.a

function M._setup()
	-- COMMAND mode
	do
		-- Save the actual line of input so we can restore it when toggling past the newest history entry
		local prev_line
		local mode = {
			name = "command",
			input = true,
			prefix = ":",
			on_return = function(line)
				lfm.mode("normal")
				api.cmdline_history_append(":", line)
				lfm.eval(line)
			end,
			on_change = function()
				prev_line = nil
				compl.reset()
			end,
		}
		lfm.register_mode(mode)
		lfm.map(":", a(lfm.mode, "command"), { desc = "enter COMMAND mode" })
		lfm.map("<Up>", function()
			if not prev_line then
				prev_line = api.cmdline_line_get()
			end
			local line = api.cmdline_history_prev()
			if line then
				api.cmdline_line_set(line)
			end
		end, { mode = "command", desc = "Previous history item" })
		lfm.map("<Down>", function()
			local line = api.cmdline_history_next()
			api.cmdline_line_set(line or prev_line)
		end, { mode = "command", desc = "Next history item" })
	end

	-- FILTER mode
	do
		local mode = {
			name = "filter",
			input = true,
			prefix = "filter: ",
			on_enter = function()
				local filter, type = api.get_filter()
				if type ~= "substring" then
					api.set_filter(filter)
				end
				api.cmdline_line_set(filter)
			end,
			on_change = function()
				api.set_filter(api.cmdline_line_get())
			end,
			on_return = function()
				lfm.mode("normal")
			end,
			on_esc = function()
				api.set_filter("")
			end,
		}
		lfm.register_mode(mode)
		lfm.map("zf", a(lfm.mode, "filter"), { desc = "Enter FILTER mode" })
		lfm.map("zF", a(lfm.feedkeys, "zf<esc>"), { desc = "Remove current filter" })
	end

	-- FUZZY mode
	do
		local mode = {
			name = "fuzzy",
			input = true,
			prefix = "fuzzy: ",
			on_enter = function()
				local filter, type = api.get_filter()
				if type ~= "fuzzy" then
					api.set_filter(filter, "fuzzy")
				end
				api.cmdline_line_set(filter)
			end,
			on_change = function()
				local filter = api.cmdline_line_get()
				api.set_filter(filter, "fuzzy")
				api.fm_top()
			end,
			on_return = function()
				lfm.mode("normal")
			end,
			on_esc = function()
				api.set_filter()
			end,
		}
		lfm.register_mode(mode)
		lfm.map("zF", a(lfm.mode, "fuzzy"), { desc = "Enter FUZZY mode" })
		lfm.map("<c-n>", api.fm_down, { mode = "fuzzy", desc = "down" })
		lfm.map("<c-p>", api.fm_up, { mode = "fuzzy", desc = "up" })
	end

	-- TRAVEL mode
	do
		local hidden
		local mode = {
			name = "travel",
			input = true,
			prefix = "travel: ",
			on_enter = function()
				hidden = options.hidden
			end,
			on_return = function()
				local file = api.current_file()
				if file then
					api.set_filter("")
					if api.fm_open() then
						lfm.mode("normal")
						lfm.eval("open")
					else
						api.cmdline_clear()
					end
				end
				if not hidden then
					options.hidden = false
				end
			end,
			on_change = function()
				local line = api.cmdline_line_get()
				if not hidden then
					if line == "." then
						options.hidden = true
					elseif line == "" then
						options.hidden = false
					end
				end
				api.set_filter(line)
			end,
			on_exit = function()
				api.set_filter("")
				if not hidden then
					options.hidden = false
				end
			end,
			on_escape = function()
				if not hidden then
					options.hidden = false
				end
			end,
		}
		lfm.register_mode(mode)
		lfm.map("f", a(lfm.mode, "travel"), { desc = "Enter TRAVEL mode" })
		lfm.map("<c-n>", api.fm_down, { mode = "travel" })
		lfm.map("<c-p>", api.fm_up, { mode = "travel" })
		lfm.map("<Up>", api.fm_up, { mode = "travel" })
		lfm.map("<Down>", api.fm_down, { mode = "travel" })
		lfm.map("<a-h>", function()
			api.set_filter("")
			api.fm_updir()
			api.cmdline_clear()
		end, { mode = "travel", desc = "Move to parent directory" })
	end

	-- TRAVEL-FUZZY mode
	do
		local hidden
		local mode = {
			name = "travel-fuzzy",
			input = true,
			prefix = "travel-fuzzy: ",
			on_enter = function()
				hidden = options.hidden
			end,
			on_return = function()
				local file = api.current_file()
				if file then
					api.set_filter()
					if api.fm_open() then
						lfm.mode("normal")
						lfm.eval("open")
					else
						api.cmdline_clear()
					end
				end
				if not hidden then
					options.hidden = false
				end
			end,
			on_change = function()
				local line = api.cmdline_line_get()
				if not hidden then
					if line == "." then
						options.hidden = true
					elseif line == "" then
						options.hidden = false
					end
				end
				api.set_filter(line, "fuzzy")
				api.fm_top()
			end,
			on_exit = function()
				api.set_filter()
				if not hidden then
					options.hidden = false
				end
			end,
			on_escape = function()
				if not hidden then
					options.hidden = false
				end
			end,
		}
		lfm.register_mode(mode)
		lfm.map("F", a(lfm.mode, "travel-fuzzy"), { desc = "Enter travel-fuzzy mode" })
		lfm.map("<c-n>", api.fm_down, { mode = "travel-fuzzy" })
		lfm.map("<c-p>", api.fm_up, { mode = "travel-fuzzy" })
		lfm.map("<Up>", api.fm_up, { mode = "travel-fuzzy" })
		lfm.map("<Down>", api.fm_down, { mode = "travel-fuzzy" })
		lfm.map("<a-h>", function()
			api.set_filter("")
			api.fm_updir()
			api.cmdline_clear()
		end, { mode = "travel-fuzzy", desc = "Move to parent directory" })
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
				file = api.current_file()
			end,
			on_change = function()
				lfm.search(api.cmdline_line_get())
				lfm.search_next(true)
			end,
			on_return = function()
				lfm.search_next(true)
				lfm.mode("normal")
			end,
			on_esc = function()
				lfm.nohighlight()
				if file then
					api.fm_sel(fs.basename(file) --[[@as string]])
				end
			end,
		}

		local mode_back = {
			name = "search-back",
			input = true,
			prefix = "?",
			on_enter = function()
				lfm.nohighlight()
				file = lfm.api.current_file()
			end,
			on_change = function()
				lfm.search_back(api.cmdline_line_get())
				lfm.search_next(true)
			end,
			on_return = function()
				lfm.search_next(true)
				lfm.mode("normal")
			end,
			on_esc = function()
				lfm.nohighlight()
				if file then
					api.fm_sel(fs.basename(file) --[[@as string]])
				end
			end,
		}
		lfm.register_mode(mode)
		lfm.register_mode(mode_back)

		lfm.map("/", a(lfm.mode, "search"), { desc = "Search" })
		lfm.map("?", a(lfm.mode, "search-back"), { desc = "Search backwards" })
		lfm.map("n", lfm.search_next, { desc = "Go to next search result" })
		lfm.map("N", lfm.search_prev, { desc = "Go to previous search result" })
	end
end

return M
