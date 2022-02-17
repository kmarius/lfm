-- DON'T use local lfm = lfm here because it brakes sumneko.

local config = lfm.config

package.path = string.gsub(package.path, "./%?.lua;", "")
local package_path = package.path
if not string.match(package.path, config.luadir) then
	package.path = package.path .. ";" .. config.luadir .. "/?.lua"
end

local fm = lfm.fm
local log = lfm.log
local ui = lfm.ui
local cmd = lfm.cmd
local nop = function() end

local home = os.getenv("HOME")

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

---Executes line. If the first whitespace delimited token is a registered
---command it is executed with the following text as arguments. Otherwise line
---is assumed to be lua code and is executed. Example:
---```
---
--- lfm.eval("cd /home") -- expression is not lua as "cd" is a registered command
--- lfm.eval('print(2+2)') -- executed as lua code
---
---```
---@param line string
function lfm.eval(line)
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

local function set(...)
	local t = {...}
	local val = true
	local opt = t[1]
	if string.sub(opt, 1, 2) == "no" then
		val = false
		opt = string.sub(opt, 3)
	elseif string.sub(opt, 1, 3) == "inv" then
		opt = string.sub(opt, 4)
		val = not config[opt]
	end
	config[opt] = val
end


-- Hooks
local hooks = {
	LfmEnter = {},
	ExitPre = {},
	ChdirPre = {},
	ChdirPost = {},
	SelectionChanged = {},
	Resized = {},
}

---Register a function to hook into events. Curruntly supported hooks are
---```
--- LfmEnter         lfm has started and read all configuration
--- ExitPre          lfm is about to exit
--- ChdirPre         emitted before changing directories
--- ChdirPost        emitted after changin directories
--- SelectionChanged the selection changed
--- Resized          the window was resized
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

-- Commands
lfm.commands = {}

---Register a function as a lfm command or unregister a command. Supported options
---```
--- tokenize: tokenize the argument by whitespace and pass them as a table (default: false)
---
---```
---@param name string Command name, can not contain whitespace.
---@param f function The function to execute or `nil` to unregister
---@param t table Additional options.
function lfm.register_command(name, f, t)
	if (f) then
		t = t or {}
		t.f = f
		t.tokenize = t.tokenize == nil and true or t.tokenize
		lfm.commands[name] = t
	else
		lfm.commands[name] = nil
	end
end

lfm.modes = {}
local modes = lfm.modes

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

-- Set up modules
local compl = require("compl")
lfm.compl = compl

local shell = require("shell")
lfm.shell = shell

lfm.register_command("shell", function(arg) shell.bash(arg, {files=shell.ARRAY})() end, {tokenize=false, compl=compl.files})
lfm.register_command("shell-bg", function(arg) shell.bash(arg, {files=shell.ARRAY, fork=true})() end, {tokenize=false, compl=compl.files})

local opener = require("opener")
lfm.opener = opener

lfm.register_command("quit", lfm.quit)
lfm.register_command("q", lfm.quit)
lfm.register_command("set", set, {tokenize=true, compl=compl.limit(1, compl.options)})
lfm.register_command("rename", require("functions").rename, {tokenize=false, compl=compl.limit(1, compl.files)})

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

---Change directory.
---@param dir string Target destination (default: $HOME).
local function cd(dir)
	dir = dir or os.getenv("HOME")
	dir = string.gsub(dir, "^~", os.getenv("HOME"))
	fm.chdir(dir)
end

lfm.register_command("cd", cd, {tokenize=true})

local handle_key = lfm.handle_key
---Feed keys into the key handler.
---@vararg string keys
function lfm.feedkeys(...)
	for _, seq in pairs({...}) do
		handle_key(seq)
	end
end

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

-- Modes
-- TODO: make functions to easily enter a mode (on 2021-07-23)
local mode_filter = {
	prefix = "filter: ",
	enter = fm.filter,
	esc = function() fm.filter("") end,
	change = function() fm.filter(cmd.getline()) end,
}

local mode_cmd = {
	prefix = ":",
	enter = function(line) ui.history_append(line) lfm.eval(line) end,
	esc = nop,
	change = compl.reset,
}

local mode_find = {
	prefix = "find: ",
	enter = function() lfm.find_clear() lfm.eval("open") end,
	esc = lfm.find_clear,
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
			if lfm.commands.open.f() then
				cmd.clear()
			end
		end
	end,
}

local mode_delete = {
	prefix = "delete [y/N]: ",
	enter = lfm.cmd.clear,
	esc = lfm.cmd.clear,
	change = function()
		local line = lfm.cmd.getline()
		lfm.cmd.clear()
		if line == "y" then
			lfm.execute({"trash-put", "--", unpack(lfm.sel_or_cur())}, {fork=true})
			fm.selection_clear()
		end
	end,
}

lfm.register_mode(require("search").mode_search)
lfm.register_mode(require("search").mode_search_back)
lfm.register_mode(mode_delete)
lfm.register_mode(mode_cmd)
lfm.register_mode(mode_filter)
lfm.register_mode(mode_find)
lfm.register_mode(mode_travel)

-- Colors
local palette = require("colors").palette
require("colors").set({
	broken = {fg = palette.red},
	patterns = {
		{
			color = {fg = palette.magenta},
			ext = {
				".mp3", ".m4a", ".ogg", ".flac", ".mka",
				".mp4", ".mkv", ".m4v", ".webm", ".avi", ".flv", ".wmv", ".mov", ".mpg", ".mpeg",
			},
		},
		{
			color = {fg = palette.bright_red},
			ext = {".tar", ".zst", ".xz", ".gz", ".zip", ".rar", ".7z", ".bz2"},
		},
		{
			color = {fg = palette.yellow},
			ext = {".jpg", ".jpeg", ".png", ".bmp", ".webp", ".gif"},
		},
	}
})

-- Keymaps

local function wrap_mode_change(f)
	return function()
		f()
		local mode = modes[cmd.getprefix()]
		if mode then
			mode.change()
		end
	end
end

local cmap = lfm.cmap
cmap("<Enter>", cmdenter, {desc="Enter"})
cmap("<Esc>", cmdesc, {desc="Esc"})
cmap("<Left>", cmd.left, {desc="Left"})
cmap("<Right>", cmd.right, {desc="Right"})
cmap("<Up>", history_prev, {desc="previous history item"})
cmap("<Down>", history_next, {desc="next history item"})
cmap("<Home>", cmd.home, {desc="Home"})
cmap("<End>", cmd._end, {desc="End"})
cmap("<c-Left>", cmd.word_left, {desc="jump word left"})
cmap("<c-Right>", cmd.word_right, {desc="jump word right"})
cmap("<Delete>", wrap_mode_change(cmd.delete_right), {desc="delete right"})
cmap("<Backspace>", wrap_mode_change(cmd.delete), {desc="delete left"})
cmap("<c-h>", wrap_mode_change(cmd.delete), {desc="delete left"})
cmap("<c-w>", wrap_mode_change(cmd.delete_word), {desc="delete word left"})
cmap("<c-Backspace>", wrap_mode_change(cmd.delete_word), {desc="delete word left"})
cmap("<c-u>", wrap_mode_change(cmd.delete_line_left), {desc="delete line left"})
cmap("<Tab>", compl.next, {desc="next completion item"})
cmap("<s-Tab>", compl.prev, {desc="previous completion item"})

local c = require("util").c
local a = require("util").a

local map = lfm.map

map("q", lfm.quit, {desc="quit"})
map("<c-q>", lfm.quit, {desc="quit"})
map("<c-l>", ui.clear, {desc="clear screen and redraw"})
map("<a-r>", fm.drop_cache, {desc="drop direcory/preview caches"})
map("cd", a(lfm.feedkeys, ":cd "), {desc=":cd "})
map("<a-c>", fm.check, {desc="check directories and reload"})

map(":", a(cmd.setprefix, ":"), {desc=":"})
map("&", a(lfm.feedkeys, ":shell-bg "), {desc=":shell-bg "})
map("s", a(lfm.feedkeys, ":shell "), {desc=":shell "})
map("S", shell.fish("env LF_LEVEL=1 fish -C clear", {files=shell.ARRAY}), {desc="open shell"})

-- Visual/selection
map("<Space>", c(fm.selection_toggle, fm.down), {desc="select current file"})
map("v", fm.selection_reverse, {desc="reverse selection"})
map("V", fm.visual_toggle, {desc="toggle visual selection mode"})
map("uv", c(fm.load_clear, fm.selection_clear), {desc="selection-clear"})

-- Navigation
map("<Enter>", opener.open, "open")
map("<Left>", fm.updir, {desc="go to parent directory"})
map("<Right>", opener.open, {desc="open file/directory"})
map("r", opener.ask, {desc="show opener options"})
map("j", fm.down, {desc="move cursor down"})
map("k", fm.up, {desc="move cursor up"})
map("h", fm.updir, {desc="go to parent directory"})
map("l", opener.open, {desc="open file/directory"})
map("L", require("functions").follow_link)
map("H", a(lfm.feedkeys, "''")) -- complementary to "L"
map("gg", fm.top, {desc="go to top"})
map("G", fm.bottom, {desc="go to bottom"})
map("''", a(fm.mark_load, "'"), {desc="jump to previous directory"})
map("cd", a(lfm.feedkeys, ":cd "), {desc=":cd "})
map("<Up>", fm.up, {desc="move cursor up"})
map("<Down>", fm.down, {desc="move cursor down"})
map("<c-y>", fm.scroll_up, {desc="scroll directory up"})
map("<c-e>", fm.scroll_down, {desc="scroll directory down"})
map("<c-u>", function() fm.up(fm.get_height()/2) end, {desc="move cursor half a page up"})
map("<c-d>", function() fm.down(fm.get_height()/2) end, {desc="move cursor half a page down"})
map("<c-b>", function() fm.up(fm.get_height()) end, {desc="move cursor half a page up"})
map("<c-f>", function() fm.down(fm.get_height()) end, {desc="move cursor half a page down"})
map("<PageUp>", function() fm.up(fm.get_height()) end, {desc="move cursor half a page up"})
map("<PageDown>", function() fm.down(fm.get_height()) end, {desc="move cursor half a page down"})
map("<Home>", fm.top, {desc="go to top"})
map("<End>", fm.bottom, {desc="go to bottom"})

-- map("F", a(cmd.setprefix, "travel: "), {desc="travel"})
map("zf", function() cmd.setprefix(mode_filter.prefix) cmd.setline(fm.getfilter()) end, {desc="filter:"})
map("zF", a(lfm.feedkeys, "zf<esc>"), {desc="remove current filter"})
map("zh", function() config.hidden = not config.hidden end, {desc="toggle hidden files"})

-- Flatten
lfm.register_command("flatten", require("flatten").flatten, {tokenize=true})
map("<a-+>", require("flatten").flatten_inc, {desc="increase flatten level"})
map("<a-->", require("flatten").flatten_dec, {desc="decrease flatten level"})

-- Find/hinting
map("f", a(cmd.setprefix, mode_find.prefix), {desc="find:"})
-- These two only make sense in find: mode. Maybe think about actually providing
-- a way to set keybinds for modes.
cmap("<c-n>", lfm.find_next, {desc="go to next find match"})
cmap("<c-p>", lfm.find_prev, {desc="go to previous find match"})

-- Search
map("/", require("search").enter_mode, {desc="search:"})
map("?", require("search").enter_mode_back, {desc="search: (backwards)"})
map("n", lfm.search_next, {desc="go to next search result"})
map("N", lfm.search_prev, {desc="go to previous search result"})

-- Copy/pasting
map("yn", require("functions").yank_name, {desc="yank name"})
map("yp", require("functions").yank_path, {desc="yank path"})
map("yy", fm.copy, {desc="copy"})
map("dd", fm.cut, {desc="cut"})
map("ud", fm.load_clear, {desc="load-clear"})
map("pp", require("functions").paste, {desc="paste-overwrite"})
map("po", require("functions").paste_overwrite, {desc="paste-overwrite"})
map("pl", require("functions").symlink, {desc="symlink"})
map("pL", require("functions").symlink_relative, {desc="symlink-relative"})
map("df", a(lfm.cmd.setprefix, mode_delete.prefix), {desc="trash-put"})
map("dD", a(lfm.cmd.setprefix, mode_delete.prefix), {desc="delete"})


-- Renaming
map("cW", a(lfm.feedkeys, ":rename "), {desc="rename"})
map("cc", a(lfm.feedkeys, ":rename "), {desc="rename"})
map("cw", require("functions").rename_until_ext, {desc="rename-until-ext"})
map("a", require("functions").rename_before_ext, {desc="rename-before-ext"})
map("A", require("functions").rename_after, {desc="rename-after"})
map("I", require("functions").rename_before, {desc="rename-before"})

map("on", a(fm.sortby, "natural", "noreverse"), {desc="sort: natural, noreverse"})
map("oN", a(fm.sortby, "natural", "reverse"), {desc="sort: natural, reverse"})
map("os", a(fm.sortby, "size", "reverse"), {desc="sort: size, noreverse"})
map("oS", a(fm.sortby, "size", "noreverse"), {desc="sort: size, reverse"})
map("oc", a(fm.sortby, "ctime", "noreverse"), {desc="sort: ctime, noreverse"})
map("oC", a(fm.sortby, "ctime", "reverse"), {desc="sort: ctime, reverse"})
map("od", a(fm.sortby, "dirfirst"), {desc="sort: dirfirst"})
map("oD", a(fm.sortby, "nodirfirst"), {desc="sort: nodirfirst"})
map("or", a(fm.sortby, "random"), {desc="sort: random"})

lfm.register_mode(require("glob").mode_glob_select)
map("*", a(lfm.cmd.setprefix, require("glob").mode_glob_select.prefix), {desc="glob-select"})
lfm.register_command("glob-select", c(require("glob").glob_select, ui.draw), {tokenize=false})
lfm.register_command("glob-select-rec", c(require("glob").glob_select_rec, ui.draw), {tokenize=false})

lfm.register_command("mark-save", require("quickmarks").mark_save)
lfm.register_mode(require("quickmarks").mode_mark_save)
lfm.map("m", a(lfm.cmd.setprefix, require("quickmarks").mode_mark_save.prefix), {desc="save quickmark"})

local function gmap(key, location)
	map("g"..key, function() cd(location) end, {desc="cd "..location})
end

gmap("h", os.getenv("HOME"))
gmap("m", "/mnt")
gmap("p", "/tmp")
gmap("n", "~/Downloads")

-- Setup package.path for the user and source the config
-- package.path = package_path
if not string.match(package.path, home.."/.config/lfm/lua/") then
	package.path = package.path .. ";"..home.."/.config/lfm/lua/?.lua"
end

if require("lfs").attributes(config.configpath) then
	dofile(config.configpath)
end
