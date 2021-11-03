local home = os.getenv("HOME")
package.path = string.gsub(package.path, "./%?.lua;", "")
if not string.match(package.path, home.."/.config/lfm/lua/") then
	package.path = package.path .. ";"..home..[[/.config/lfm/lua/?.lua]]
end
if not string.match(package.cpath, home.."/.config/lfm/lua/") then
	package.cpath = package.cpath .. ";"..home..[[/.config/lfm/libs/?.so]]
end

local fm = lfm.fm
local log = lfm.log
local ui = lfm.ui
local config = lfm.config
local cmd = lfm.cmd
local nop = function() end

-- enhance logging functions
for k, f in pairs(log) do
	log[k] = function(...)
		local t = {...}
		for i, e in pairs(t) do
			t[i] = tostring(e)
		end
		f(table.concat(t, " "))
	end
end

function print(...)
	local t = {...}
	for i, e in pairs(t) do
		t[i] = tostring(e)
	end
	lfm.echo(table.concat(t, " "))
end

---@return table selection The currently selected files or the file at the current cursor position
function lfm.sel_or_cur()
	local sel = fm.selection_get()
	return #sel > 0 and sel or {fm.current_file()}
end

local hooks = {
	LfmEnter = {},
	ExitPre = {},
	ChdirPre = {},
	ChdirPost = {},
	SelectionChanged = {},
}

---Register a function to hook into events. Curruntly supported hooks are
---```
--- LfmEnter         lfm has started and read all configuration
--- ExitPre          lfm is about to exit
--- ChdirPre         emitted before changing directories
--- ChdirPost        emitted after changin directories
--- SelectionChanged the selection changed
---
---```
---@param name string
---@param f function
function lfm.register_hook(name, f)
	if hooks[name] then
		table.insert(hooks[name], f)
	end
end

---Execute all functions registered to a hook.
---@param name string
function lfm.run_hook(name)
	log.debug("running hook: " .. name)
	if hooks[name] then
		for _, f in pairs(hooks[name]) do
			f()
		end
	end
end

local commands = lfm.commands
lfm.modes = {}
local modes = lfm.modes

---Register a function as a lfm command. Supported options
---```
--- tokenize: tokenize the argument by whitespace and pass them as a table (default: false)
---
---```
---@param name string Command name, can not contain whitespace.
---@param f function The function to execute
---@param t table Additional options.
function lfm.register_command(name, f, t)
	t = t or {}
	t.f = f
	t.tokenize = t.tokenize == nil and true or t.tokenize
	lfm.commands[name] = t
end

---Register a mode to lfm. A mode is given by a table t that should contain the following fields:
---```
--- t.prefix  The prefix, a string, shown in the command line and used to distinguish modes.
--- t.enter   A function that is executed when pressing enter while the mode is active.
--- t.esc     A function that is executed when pressing esc while the mode is active.
--- t.change  A function that is executed when the command line changes, e.g. keys are typed/deleted.
---
---```
---@param t table
function lfm.register_mode(t)
	modes[t.prefix] = t
end

---Function for <enter> in command mode. Clears the command line and calls `mode.enter`.
local function cmdenter()
	local line = cmd.getline()
	local prefix = cmd.getprefix()
	cmd.clear()
	local mode = modes[prefix]
	-- TODO: allow line to be "" ? (on 2021-07-23)
	if line ~= "" and mode then
		mode.enter(line)
	end
end

---Function for <esc> in command mode. Clears the command line and calls `mode.esc`.
local function cmdesc()
	local mode = modes[cmd.getprefix()]
	if mode then
		mode.esc()
	end
	cmd.clear()
end

---Function for <delete> in command mode. Deletes to the left and calls `mode.change`.
local function cmddelete()
	cmd.delete()
	local mode = modes[cmd.getprefix()]
	if mode then
		mode.change()
	end
end

---Function for <deleteright> in command mode. Deletes to the right and calls `mode.change`.
local function cmddeleteright()
	cmd.delete_right()
	local mode = modes[cmd.getprefix()]
	if mode then
		mode.change()
	end
end

---Change directory.
---@param dir string Target destination (default: $HOME).
local function cd(dir)
	fm.chdir(dir or os.getenv("HOME"))
end

---Navigate into the directory at the current cursor position.
---@return boolean false
local function open()
	local file = fm.open()
	if file then
		lfm.error("no opener configured")
	end
	return false
end

local handle_key = lfm.handle_key
---Feed keys into the key handler.
---@vararg string keys
function lfm.feedkeys(...)
	for _, seq in pairs({...}) do
		handle_key(seq)
	end
end

lfm.commands = {
	cd = {f=cd, tokenize=true},
	quit = {f=lfm.quit, tokenize=true},
}

---Fill command line with the previous history item.
local function history_prev()
	if cmd.getprefix() ~= ":" then
		return
	end
	local line = ui.history_prev()
	if line then
		cmd.setline(line)
	end
end

---Fill command line with the next history item.
local function history_next()
	if cmd.getprefix() ~= ":" then
		return
	end
	local line = ui.history_next()
	if line then
		cmd.setline(line)
	end
end

local compl = require("compl")

local cmap = lfm.cmap
cmap("<enter>", cmdenter, {desc=""})
cmap("<esc>", cmdesc, {desc=""})
cmap("<backspace>", cmddelete, {desc=""})
cmap("<left>", cmd.left, {desc=""})
cmap("<right>", cmd.right, {desc=""})
cmap("<up>", history_prev, {desc=""})
cmap("<down>", history_next, {desc=""})
cmap("<home>", cmd.home, {desc=""})
cmap("<end>", cmd._end, {desc=""})
cmap("<delete>", cmddeleteright, {desc=""})
cmap("<tab>", compl.next, {desc=""})
cmap("<s-tab>", compl.prev, {desc=""})
cmap("<c-w>", cmd.delete_word, {desc=""})

local map = lfm.map
map("f", function() cmd.setprefix("find: ") end, {desc="find"})
map("F", function() cmd.setprefix("travel: ") end, {desc="travel"})
map("zf", function() cmd.setprefix("filter: ") cmd.setline(fm.getfilter()) end, {desc="filter"})
map("l", open)
map("q", lfm.quit)
map("j", fm.down)
map("k", fm.up)
map("h", fm.updir)
map("gg", fm.top, {desc="top"})
map("G", fm.bottom, {desc="bottom"})
map("R", function() dofile("/home/marius/Sync/programming/lfm/core.lua") end, {desc="reload config"})
map("''", function() fm.mark_load("'") end)
map("zh", function() config.hidden = not config.hidden end, {desc="toggle hidden"})
map(":", function() cmd.setprefix(":") end)
map("/", function() cmd.setprefix("/") lfm.search("") end)
map("?", function() cmd.setprefix("?") lfm.search("") end)
map("n", lfm.search_next)
map("N", lfm.search_prev)

-- TODO: make functions to easily enter a mode (on 2021-07-23)
local mode_filter = {
	prefix = "filter: ",
	enter = function(line) fm.filter(line) end,
	esc = function() fm.filter("") end,
	change = function() fm.filter(cmd.getline()) end,
}

---Executes line. If the first whitespace delimited token is a registered
---command it is executed with the following text as arguments. Otherwise line
---is assumed to be lua code and is executed. Example:
---```
---
--- lfm.exec_expr("cd /home") -- expression is not lua as "cd" is a registered command
--- lfm.exec_expr('print(2+2)') -- executed as lua code
---
---```
---@param line string
function lfm.exec_expr(line)
	local cmd, args = lfm.fn.tokenize(line)
	if not cmd then
		return
	end
	local command = lfm.commands[cmd]
	if command then
		if command.tokenize then
			command.f(unpack(args))
		else
			local arg = string.gsub(line, "^[^ ]*%s*", "")
			command.f(arg)
		end
	else
		log.debug("loadstring: " .. line)
		local bc, err = loadstring(line)
		if bc then
			bc()
		else
			lfm.error("loadstring: "..err)
		end
	end
end

local mode_cmd = {
	prefix = ":",
	enter = function(line) ui.history_append(line) lfm.exec_expr(line) end,
	esc = nop,
	change = function() compl.reset() end,
}

local mode_search = {
	prefix = "/",
	enter = function() lfm.search_next(true) end, -- apply search, keep highlights, move cursor to next match  or stay on current
	esc = function() lfm.search("") end, -- delete everything
	change = function() lfm.search(cmd.getline()) end, -- highlight match in UI
}

local mode_search_back = {
	prefix = "?",
	enter = function() lfm.search_next(true) end,
	esc = function() lfm.search_back("") end,
	change = function() lfm.search_back(cmd.getline()) end,
}

local mode_find = {
	prefix = "find: ",
	enter = function() lfm.exec_expr("open") end,
	esc = nop,
	change = function()
		if lfm.find(cmd.getline()) then
			cmd.clear()
			lfm.timeout(250)
			lfm.commands.open.f()
		end
	end,
}

local mode_travel = {
	prefix = "travel: ",
	enter = nop,
	esc = nop,
	change = function()
		if lfm.find(cmd.getline()) then
			lfm.timeout(250)
			cmd.setline("")
			if commands.open.f() then
				cmd.clear()
			end
		end
	end,
}

lfm.register_mode(mode_search)
lfm.register_mode(mode_search_back)
lfm.register_mode(mode_cmd)
lfm.register_mode(mode_filter)
lfm.register_mode(mode_find)
lfm.register_mode(mode_travel)

dofile(config.configpath)
