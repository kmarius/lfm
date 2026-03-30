local M = { _NAME = ... }

local lfm = lfm

local options = lfm.o
local api = lfm.api
local fm = lfm.fm
local fs = lfm.fs
local complete = require("lfm.complete")
local util = require("lfm.util")

local a = util.a

-- COMMAND mode
do
	-- Save the actual line of input so we can restore it when toggling past the newest history entry
	local prev_line
	local mode = {
		name = "command",
		input = true,
		prefix = ":",
		on_return = function(line)
			api.mode("normal")
			api.cmdline_history_append(":", line)
			lfm.eval(line)
		end,
		on_change = function()
			prev_line = nil
			complete.reset()
		end,
	}
	api.create_mode(mode)
	api.set_keymap(":", a(api.mode, "command"), { desc = "enter COMMAND mode" })
	api.set_keymap("<Up>", function()
		if not prev_line then
			prev_line = api.cmdline_line_get()
		end
		local line = api.cmdline_history_prev()
		if line then
			api.cmdline_line_set(line)
		end
	end, { mode = "command", desc = "Previous history item" })
	api.set_keymap("<Down>", function()
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
			local filter, type = fm.get_filter()
			if type ~= "substring" then
				fm.set_filter(filter)
			end
			api.cmdline_line_set(filter)
		end,
		on_change = function()
			fm.set_filter(api.cmdline_line_get())
		end,
		on_return = function()
			api.mode("normal")
		end,
		on_esc = function()
			fm.set_filter("")
		end,
	}
	api.create_mode(mode)
	api.set_keymap("zf", a(api.mode, "filter"), { desc = "Enter FILTER mode" })
	api.set_keymap("zF", a(api.feedkeys, "zf<esc>"), { desc = "Remove current filter" })
end

-- FUZZY mode
do
	local mode = {
		name = "fuzzy",
		input = true,
		prefix = "fuzzy: ",
		on_enter = function()
			local filter, type = fm.get_filter()
			if type ~= "fuzzy" then
				fm.set_filter(filter, "fuzzy")
			end
			api.cmdline_line_set(filter)
		end,
		on_change = function()
			local filter = api.cmdline_line_get()
			fm.set_filter(filter, "fuzzy")
			fm.top()
		end,
		on_return = function()
			api.mode("normal")
		end,
		on_esc = function()
			fm.set_filter()
		end,
	}
	api.create_mode(mode)
	api.set_keymap("zF", a(api.mode, "fuzzy"), { desc = "Enter FUZZY mode" })
	api.set_keymap("<c-n>", fm.down, { mode = "fuzzy", desc = "down" })
	api.set_keymap("<c-p>", fm.up, { mode = "fuzzy", desc = "up" })
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
			local file = fm.current_file()
			if file then
				fm.set_filter("")
				if fm.open() then
					api.mode("normal")
					lfm.eval("open")
				else
					api.cmdline_line_set()
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
			fm.set_filter(line)
		end,
		on_exit = function()
			fm.set_filter("")
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
	api.create_mode(mode)
	api.set_keymap("f", a(api.mode, "travel"), { desc = "Enter TRAVEL mode" })
	api.set_keymap("<c-n>", fm.down, { mode = "travel" })
	api.set_keymap("<c-p>", fm.up, { mode = "travel" })
	api.set_keymap("<Up>", fm.up, { mode = "travel" })
	api.set_keymap("<Down>", fm.down, { mode = "travel" })
	api.set_keymap("<a-h>", function()
		fm.set_filter("")
		fm.updir()
		api.cmdline_line_set()
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
			local file = fm.current_file()
			if file then
				fm.set_filter()
				if fm.open() then
					api.mode("normal")
					lfm.eval("open")
				else
					api.cmdline_line_set()
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
			fm.set_filter(line, "fuzzy")
			fm.top()
		end,
		on_exit = function()
			fm.set_filter()
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
	api.create_mode(mode)
	api.set_keymap("F", a(api.mode, "travel-fuzzy"), { desc = "Enter travel-fuzzy mode" })
	api.set_keymap("<c-n>", fm.down, { mode = "travel-fuzzy" })
	api.set_keymap("<c-p>", fm.up, { mode = "travel-fuzzy" })
	api.set_keymap("<Up>", fm.up, { mode = "travel-fuzzy" })
	api.set_keymap("<Down>", fm.down, { mode = "travel-fuzzy" })
	api.set_keymap("<a-h>", function()
		fm.set_filter("")
		fm.updir()
		api.cmdline_line_set()
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
			file = fm.current_file()
		end,
		on_change = function()
			lfm.search(api.cmdline_line_get())
			lfm.search_next(true)
		end,
		on_return = function()
			lfm.search_next(true)
			api.mode("normal")
		end,
		on_esc = function()
			lfm.nohighlight()
			if file then
				fm.select(fs.basename(file) --[[@as string]])
			end
		end,
	}

	local mode_back = {
		name = "search-back",
		input = true,
		prefix = "?",
		on_enter = function()
			lfm.nohighlight()
			file = api.current_file()
		end,
		on_change = function()
			lfm.search_back(api.cmdline_line_get())
			lfm.search_next(true)
		end,
		on_return = function()
			lfm.search_next(true)
			api.mode("normal")
		end,
		on_esc = function()
			lfm.nohighlight()
			if file then
				fm.select(fs.basename(file) --[[@as string]])
			end
		end,
	}
	api.create_mode(mode)
	api.create_mode(mode_back)

	api.set_keymap("/", a(api.mode, "search"), { desc = "Search" })
	api.set_keymap("?", a(api.mode, "search-back"), { desc = "Search backwards" })
	api.set_keymap("n", lfm.search_next, { desc = "Go to next search result" })
	api.set_keymap("N", lfm.search_prev, { desc = "Go to previous search result" })
end

return M
