local M = { _NAME = ... }

local lfm = lfm

local cmd = lfm.cmd
local config = lfm.config
local map = lfm.map
local fm = lfm.fm
local compl = require("lfm.compl")
local util = require("lfm.util")
local basename = util.basename

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
				cmd.history_append(":", line)
				lfm.eval(line)
			end,
			on_change = function()
				prev_line = nil
				compl.reset()
			end,
		}
		lfm.register_mode(mode)
		map(":", a(lfm.mode, "command"), { desc = "enter COMMAND mode" })
		map("<Up>", function()
			if not prev_line then
				prev_line = cmd.line_get()
			end
			local line = cmd.history_prev()
			if line then
				cmd.line_set(line)
			end
		end, { mode = "command", desc = "Previous history item" })
		map("<Down>", function()
			local line = cmd.history_next()
			cmd.line_set(line or prev_line)
		end, { mode = "command", desc = "Next history item" })
	end

	-- FILTER mode
	do
		local mode = {
			name = "filter",
			input = true,
			prefix = "filter: ",
			on_enter = function()
				local filter, type = fm.getfilter()
				if type ~= "substring" then
					fm.filter(filter)
				end
				cmd.line_set(filter)
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

	-- FUZZY mode
	do
		local mode = {
			name = "fuzzy",
			input = true,
			prefix = "fuzzy: ",
			on_enter = function()
				local filter, type = fm.getfilter()
				if type ~= "fuzzy" then
					fm.filter(filter, "fuzzy")
				end
				cmd.line_set(filter)
			end,
			on_change = function()
				local filter = cmd.line_get()
				fm.filter(filter, "fuzzy")
				fm.top()
			end,
			on_return = function()
				lfm.mode("normal")
			end,
			on_esc = function()
				fm.filter()
			end,
		}
		lfm.register_mode(mode)
		map("zF", a(lfm.mode, "fuzzy"), { desc = "Enter FUZZY mode" })
		map("<c-n>", fm.down, { mode = "fuzzy", desc = "down" })
		map("<c-p>", fm.up, { mode = "fuzzy", desc = "up" })
	end

	-- TRAVEL mode
	do
		local hidden
		local mode = {
			name = "travel",
			input = true,
			prefix = "travel: ",
			on_enter = function()
				hidden = config.hidden
			end,
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
				if not hidden then
					config.hidden = false
				end
			end,
			on_change = function()
				local line = cmd.line_get()
				if not hidden then
					if line == "." then
						config.hidden = true
					elseif line == "" then
						config.hidden = false
					end
				end
				fm.filter(line)
			end,
			on_exit = function()
				fm.filter("")
				if not hidden then
					config.hidden = false
				end
			end,
			on_escape = function()
				if not hidden then
					config.hidden = false
				end
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

	-- TRAVEL-FUZZY mode
	do
		local hidden
		local mode = {
			name = "travel-fuzzy",
			input = true,
			prefix = "travel-fuzzy: ",
			on_enter = function()
				hidden = config.hidden
			end,
			on_return = function()
				local file = fm.current_file()
				if file then
					fm.filter()
					if fm.open() then
						lfm.mode("normal")
						lfm.eval("open")
					else
						cmd.clear()
					end
				end
				if not hidden then
					config.hidden = false
				end
			end,
			on_change = function()
				local line = cmd.line_get()
				if not hidden then
					if line == "." then
						config.hidden = true
					elseif line == "" then
						config.hidden = false
					end
				end
				fm.filter(line, "fuzzy")
				fm.top()
			end,
			on_exit = function()
				fm.filter()
				if not hidden then
					config.hidden = false
				end
			end,
			on_escape = function()
				if not hidden then
					config.hidden = false
				end
			end,
		}
		lfm.register_mode(mode)
		map("F", a(lfm.mode, "travel-fuzzy"), { desc = "Enter travel-fuzzy mode" })
		map("<c-n>", fm.down, { mode = "travel-fuzzy" })
		map("<c-p>", fm.up, { mode = "travel-fuzzy" })
		map("<Up>", fm.up, { mode = "travel-fuzzy" })
		map("<Down>", fm.down, { mode = "travel-fuzzy" })
		map("<a-h>", function()
			fm.filter("")
			fm.updir()
			cmd.clear()
		end, { mode = "travel-fuzzy", desc = "Move to parent directory" })
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
					-- local command = { "trash-put", "--" }
					local command = { "trash-put" }
					for _, file in ipairs(fm.sel_or_cur()) do
						command[#command + 1] = file
					end
					lfm.spawn(command)
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
					fm.sel(basename(file) --[[@as string]])
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
					lfm.fm.sel(basename(file) --[[@as string]])
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
