-- DON'T use local lfm = lfm here because it brakes sumneko.

local config = lfm.config

package.path = string.gsub(package.path, "./%?.lua;", "")
package.path = package.path .. ";" .. config.luadir .. "/?.lua"
package.path = package.path .. ";".. config.configdir .. "/lua/?.lua"

local fm = lfm.fm
local log = lfm.log
local ui = lfm.ui
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
			command.f(arg ~= "" and arg or nil)
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

-- Hooks
local hooks = {
	LfmEnter = {},
	ExitPre = {},
	ChdirPre = {},
	ChdirPost = {},
	SelectionChanged = {},
	Resized = {},
	PasteBufChange = {},
	DirLoaded = {},
	DirUpdated = {},
}

---@alias Lfm.Hook
---| '"LfmEnter"'
---| '"ExitPre"'
---| '"ChdirPre"'
---| '"ChdirPost"'
---| '"SelectionChanged"'
---| '"Resized"'
---| '"PasteBufChange"'
---| '"DirLoaded"'
---| '"DirUpdated"'

---Register a function to hook into events. Curruntly supported hooks are
---```
--- LfmEnter         lfm has started and read all configuration
--- ExitPre          lfm is about to exit
--- ChdirPre         emitted before changing directories
--- ChdirPost        emitted after changin directories
--- SelectionChanged the selection changed
--- Resized          the window was resized
--- PasteBufChange   the paste buffer changed
--- DirLoaded        a new directory was loaded from disk
---
---```
---@param name Lfm.Hook
---@param f function
function lfm.register_hook(name, f)
	if hooks[name] then
		table.insert(hooks[name], f)
	end
end

---Execute all functions registered to a hook.
---@param name Lfm.Hook
function lfm.run_hook(name, ...)
	log.debug("running hook: " .. name)
	if hooks[name] then
		for _, f in pairs(hooks[name]) do
			f(...)
		end
	end
end

-- Commands
lfm.commands = {}

---@class Lfm.CommandParams
---@field tokenize boolean tokenize arguments (default: true)
---@field compl function completion function

---Register a function as a lfm command or unregister a command. Supported options
---```
--- tokenize: tokenize the argument by whitespace and pass them as a table (default: false)
--- compl:    completion function
---
---```
---@param name string Command name, can not contain whitespace.
---@param f function The function to execute or `nil` to unregister
---@param t? Lfm.CommandParams Additional options.
function lfm.register_command(name, f, t)
	if f then
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

---@class Lfm.ModeDef
---@field prefix string
---@field on_enter function
---@field on_esc function
---@field on_change function

---Register a mode to lfm. A mode is given by a table t containing the following fields:
---```
--- t.prefix     The prefix, a string, shown in the command line and used to distinguish modes.
--- t.on_enter   A function that is called when pressing enter while the mode is active.
--- t.on_esc     A function that is called when pressing esc while the mode is active.
--- t.on_change  A function that is called when the command line changes, e.g. keys are typed/deleted.
---```
---@param t Lfm.ModeDef
function lfm.register_mode(t)
	t = t or {}
	assert(t.prefix ~= nil, "no prefix given")
	modes[t.prefix] = t
end

-- Set up modules
local compl = require("compl")
lfm.compl = compl

lfm.inspect = require("inspect")

local shell = require("shell")
lfm.shell = shell

lfm.register_command("shell", function(arg) shell.bash(arg, {files=shell.ARRAY})() end, {tokenize=false, compl=compl.files})
lfm.register_command("shell-bg", function(arg) shell.bash(arg, {files=shell.ARRAY, fork=true})() end, {tokenize=false, compl=compl.files})

require("jumplist").setup()

lfm.register_command("quit", lfm.quit)
lfm.register_command("q", lfm.quit)
lfm.register_command("rename", require("functions").rename, {tokenize=false, compl=compl.limit(1, compl.files)})

---Function for <enter> in command mode. Clears the command line and calls `mode.enter`.
local function cmdenter()
	local line = cmd.line_get()
	local prefix = cmd.prefix_get()
	cmd.clear()
	local mode = modes[prefix]
	-- TODO: allow line to be "" ? (on 2021-07-23)
	if line ~= "" and mode then
		local mode_enter = mode.on_enter
		if mode_enter then
			mode_enter(line)
		end
	end
end

---Function for <esc> in command mode. Clears the command line and calls `mode.esc`.
local function cmdesc()
	local mode = modes[cmd.prefix_get()]
	if mode then
		local on_esc = mode.on_esc
		if on_esc then
			on_esc()
		end
	end
	cmd.clear()
end

lfm.register_command("cd", fm.chdir, {tokenize=true, compl=compl.dirs})

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
	if cmd.prefix_get() ~= ":" then
		return
	end
	local line = cmd.history_prev()
	if line then
		cmd.line_set(line)
	end
end

---Fill command line with the next history item.
local function history_next()
	if cmd.prefix_get() ~= ":" then
		return
	end
	local line = cmd.history_next()
	if line then
		cmd.line_set(line)
	end
end

-- Modes
-- TODO: make functions to easily enter a mode (on 2021-07-23)
local mode_filter = {
	prefix = "filter: ",
	on_enter = fm.filter,
	on_esc = function() fm.filter("") end,
	on_change = function() fm.filter(cmd.line_get()) end,
}

local mode_cmd = {
	prefix = ":",
	on_enter = function(line) cmd.history_append(":", line) lfm.eval(line) end,
	on_esc = nop,
	on_change = compl.reset,
}

local mode_find = {
	prefix = "find: ",
	on_enter = function() lfm.find_clear() lfm.eval("open") end,
	on_esc = lfm.find_clear,
	on_change = function()
		if lfm.find(cmd.line_get()) then
			cmd.clear()
			lfm.timeout(250)
			lfm.commands.open.f()
		end
	end,
}

local mode_travel = {
	prefix = "travel: ",
	on_enter = function()
		local file = fm.current_file()
		if file then
			fm.filter("")
			if fm.open() then
				lfm.eval("open")
			else
				cmd.prefix_set("travel: ")
			end
		end
	end,
	on_esc = function() fm.filter("") end,
	on_change = function()
		local line = cmd.line_get()
		fm.filter(line)
	end,
}

local mode_delete = {
	prefix = "delete [y/N]: ",
	on_enter = lfm.cmd.clear,
	on_esc = lfm.cmd.clear,
	on_change = function()
		local line = lfm.cmd.line_get()
		lfm.cmd.clear()
		if line == "y" then
			lfm.spawn({"trash-put", "--", unpack(lfm.sel_or_cur())})
			fm.selection_set()
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
				".mp4", ".mkv", ".m4v", ".webm", ".avi", ".flv", ".wmv", ".mov", ".mpg", ".mpeg", ".3gp",
			},
		},
		{
			color = {fg = palette.bright_red},
			ext = {
				".tar", ".zst", ".xz", ".gz", ".zip", ".rar", ".7z", ".bz2",
			},
		},
		{
			color = {fg = palette.yellow},
			ext = {
				".jpg", ".jpeg", ".png", ".bmp", ".webp", ".gif",
			},
		},
	}
})

lfm.rifle = require("rifle")

-- Opener: xdg-open. Registering a new "open" command overrides.

lfm.register_command("open", function()
	local file = lfm.fm.open()
	if file then
		local files = lfm.fm.selection_get()
		if #files == 0 then
			files = {file}
		end
		lfm.execute({"xdg-open", unpack(files)})
	end
end)

local function open()
	lfm.eval("open")
end

-- Keymaps

local function wrap_mode_change(f)
	return function()
		f()
		local mode = modes[cmd.prefix_get()]
		if mode then
			local on_change = mode.on_change()
			if on_change then
				on_change()
			end
		end
	end
end

local cmap = lfm.cmap
cmap("<Enter>", cmdenter, {desc="Enter"})
cmap("<Insert>", cmd.toggle_overwrite, {desc="Toggle insert/overwrite"})
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
map("ZZ", lfm.quit, {desc="quit"})
-- maybe don't write selection/lastdir when exiting with ZQ/:q!
map("ZQ", lfm.quit, {desc="quit"})
map("<c-q>", lfm.quit, {desc="quit"})
map("<c-c>", function() print("Type :q <Enter> or <Ctrl>q to exit") end, {desc="ctrl-c"})
map("<c-l>", ui.clear, {desc="clear screen and redraw"})
map("<a-r>", fm.drop_cache, {desc="drop direcory/preview caches"})
map("cd", a(lfm.feedkeys, ":cd "), {desc=":cd "})
map("<a-c>", fm.check, {desc="check directories and reload"})

map(":", a(cmd.prefix_set, ":"), {desc=":"})
map("&", a(lfm.feedkeys, ":shell-bg "), {desc=":shell-bg "})
map("s", a(lfm.feedkeys, ":shell "), {desc=":shell "})
map("S", a(lfm.execute, {"sh", "-c", "LFM_LEVEL=1 " .. os.getenv("SHELL")}), {desc="open shell"})

-- Visual/selection
map("<Space>", c(fm.selection_toggle, fm.down), {desc="select current file"})
map("v", fm.selection_reverse, {desc="reverse selection"})
map("V", fm.visual_toggle, {desc="toggle visual selection mode"})
map("uv", c(fm.paste_buffer_set, fm.selection_set), {desc="selection-clear"})

-- Navigation
map("<Enter>", open, {desc="open"})
map("<Left>", fm.updir, {desc="go to parent directory"})
map("<Right>", open, {desc="open file/directory"})
map("j", fm.down, {desc="move cursor down"})
map("k", fm.up, {desc="move cursor up"})
map("h", fm.updir, {desc="go to parent directory"})
map("l", open, {desc="open file/directory"})
map("L", require("functions").follow_link)
map("H", a(lfm.feedkeys, "''")) -- complementary to "L"
map("gg", fm.top, {desc="go to top"})
map("G", fm.bottom, {desc="go to bottom"})
map("''", fm.jump_automark, {desc="jump to previous directory"})
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

map("F", a(cmd.prefix_set, "travel: "), {desc="travel"})
map("zf", function() cmd.prefix_set(mode_filter.prefix) cmd.line_set(fm.getfilter()) end, {desc="filter:"})
map("zF", a(lfm.feedkeys, "zf<esc>"), {desc="remove current filter"})
map("zh", function() config.hidden = not config.hidden end, {desc="toggle hidden files"})

-- Flatten
lfm.register_command("flatten", require("flatten").flatten, {tokenize=true})
map("<a-+>", require("flatten").flatten_inc, {desc="increase flatten level"})
map("<a-->", require("flatten").flatten_dec, {desc="decrease flatten level"})

-- Find/hinting
map("f", a(cmd.prefix_set, mode_find.prefix), {desc="find:"})
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
map("ud", fm.paste_buffer_clear, {desc="load-clear"})
map("pp", require("functions").paste, {desc="paste-overwrite"})
map("pt", require("functions").paste_toggle, {desc="toggle paste mode"})
map("po", require("functions").paste_overwrite, {desc="paste-overwrite"})
map("pl", require("functions").symlink, {desc="symlink"})
map("pL", require("functions").symlink_relative, {desc="symlink-relative"})
map("df", a(lfm.cmd.prefix_set, mode_delete.prefix), {desc="trash-put"})
map("dD", a(lfm.cmd.prefix_set, mode_delete.prefix), {desc="delete"})


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
map("*", a(lfm.cmd.prefix_set, require("glob").mode_glob_select.prefix), {desc="glob-select"})
lfm.register_command("glob-select", require("glob").glob_select, {tokenize=false})
lfm.register_command("glob-select-rec", require("glob").glob_select_recursive, {tokenize=false})

lfm.register_command("mark-save", require("quickmarks").mark_save)
lfm.register_command("mark-delete", require("quickmarks").mark_delete)
lfm.register_mode(require("quickmarks").mode_mark_save)
lfm.register_mode(require("quickmarks").mode_mark_delete)
lfm.map("m", a(lfm.cmd.prefix_set, require("quickmarks").mode_mark_save.prefix), {desc="save quickmark"})
lfm.map("dm", a(lfm.cmd.prefix_set, require("quickmarks").mode_mark_delete.prefix), {desc="delete quickmark"})

local function gmap(key, location)
	map("g"..key, function() fm.chdir(location) end, {desc="cd "..location})
end

gmap("/", "/")
gmap("e", "/etc")
gmap("h", os.getenv("HOME"))
gmap("m", "/mnt")
gmap("n", "~/Downloads")
gmap("o", "/opt")
gmap("p", "/tmp")
gmap("r", "/")
gmap("s", "/srv")
gmap("u", "/usr")

local cfg = loadfile(config.configpath)
if cfg then
	cfg()
end
