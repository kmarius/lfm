-- DON'T use local lfm = lfm here because it brakes sumneko.

local config = lfm.config

package.path = string.gsub(package.path, "./%?.lua;", "")
package.path = package.path .. ";" .. config.configdir .. "/lua/?.lua"

local fm = lfm.fm
local log = lfm.log
local ui = lfm.ui
local cmd = lfm.cmd

-- enhance logging functions
for k, f in pairs(log) do
	if type(f) == "function" then
		log[k] = function(...)
			local t = { ... }
			for i, e in pairs(t) do
				t[i] = tostring(e)
			end
			f(table.concat(t, " "))
		end
	end
end

---@return table selection The currently selected files or the file at the current cursor position
function lfm.sel_or_cur()
	local sel = fm.selection_get()
	return #sel > 0 and sel or { fm.current_file() }
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
			local arg = string.gsub(line, "^%s*[^ ]*%s*", "")
			command.f(arg ~= "" and arg or nil)
		end
	else
		log.debug("loadstring: " .. line)
		if string.sub(line, 1, 1) == "=" then
			line = "return " .. string.sub(line, 2, -1)
		end
		local bc, err = loadstring(line)
		if bc then
			local res = bc()
			if res then
				print(res)
			end
		else
			lfm.error("loadstring: " .. err)
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
	log.debug("registering hook: " .. name)
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
---@field tokenize? boolean tokenize arguments (default: true)
---@field compl? function completion function
---@field desc? string Description

---Register a function as a lfm command or unregister a command. Supported options
---```
--- tokenize: tokenize the argument by whitespace and pass them as a table (default: false)
--- compl:    completion function
--- desc:     Description
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

-- submodule setup

-- lazily load submodules in the lfm namespace
local submodules = {
	compl = true,
	inspect = true,
	functions = true,
	jumplist = true,
	quickmarks = true,
	rifle = true,
	search = true,
	shell = true,
	util = true,
}

setmetatable(lfm, {
	__index = function(t, key)
		if submodules[key] then
			t[key] = require("lfm." .. key)
			return t[key]
		end
	end,
})

require("lfm.rifle").setup({
	rules = {
		'mime inode/x-empty, label editor, has $EDITOR = $EDITOR -- "$@"',
		'mime ^text, label editor, has $EDITOR = $EDITOR -- "$@"',
		'mime ^text, label pager, has $PAGER = $PAGER -- "$@"',
	},
})

local util = require("lfm.util")
local compl = require("lfm.compl")
local shell = require("lfm.shell")

lfm.register_command("shell", function(arg)
	shell.bash(arg, { files = shell.ARRAY })()
end, { tokenize = false, compl = compl.files, desc = "Run a shell command." })
lfm.register_command("shell-bg", function(arg)
	shell.bash(arg, { files = shell.ARRAY, fork = true })()
end, { tokenize = false, compl = compl.files, desc = "Run a shell command in the background." })

require("lfm.jumplist")._setup()
require("lfm.quickmarks")._setup()

lfm.register_command("quit", lfm.quit, { desc = "Quit Lfm." })
lfm.register_command("q", lfm.quit, { desc = "Quit Lfm." })
lfm.register_command(
	"rename",
	require("lfm.functions").rename,
	{ tokenize = false, compl = compl.limit(1, compl.files), desc = "Rename the current file." }
)

lfm.register_command("cd", fm.chdir, { tokenize = true, compl = compl.dirs })

local handle_key = lfm.handle_key
---Feed keys into the key handler.
---@vararg string keys
function lfm.feedkeys(...)
	for _, seq in pairs({ ... }) do
		handle_key(seq)
	end
end

local map = lfm.map
local cmap = lfm.cmap

local c = util.c
local a = util.a

-- COMMAND mode

local mode_command = {
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
lfm.register_mode(mode_command)
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

-- FILTER mode
local mode_filter = {
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
		fm.filter(cmd.line_get())
		lfm.mode("normal")
	end,
	on_esc = function()
		fm.filter("")
	end,
}
lfm.register_mode(mode_filter)
map("zf", a(lfm.mode, "filter"), { desc = "Enter FILTER mode" })
map("zF", a(lfm.feedkeys, "zf<esc>"), { desc = "Remove current filter" })

-- TRAVEL mode
local mode_travel = {
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
				--lfm.mode("travel")
			end
		end
	end,
	on_esc = function()
		fm.filter("")
	end,
	on_change = function()
		local line = cmd.line_get()
		fm.filter(line)
	end,
}
lfm.register_mode(mode_travel)
map("f", a(lfm.mode, "travel"), { desc = "Enter TRAVEL mode" })
map("<c-n>", fm.down, { mode = "travel" })
map("<c-p>", fm.up, { mode = "travel" })
map("<a-h>", fm.updir, { mode = "travel" })

-- DELETE mode
local has_trash = os.execute("command -v trash-put >/dev/null") == 0

local mode_delete = {
	name = "delete",
	input = true,
	prefix = "delete [y/N]: ",
	on_return = function()
		lfm.mode("normal")
	end,
	on_esc = cmd.clear,
	on_change = function()
		local line = cmd.line_get()
		lfm.mode("normal")
		if line == "y" then
			lfm.spawn({ "trash-put", "--", unpack(lfm.sel_or_cur()) })
			fm.selection_set()
		end
	end,
}
lfm.register_mode(mode_delete)
if has_trash then
	map("df", a(lfm.mode, "delete"), { desc = "Trash file/selection" })
end

-- SEARCH mode
lfm.register_mode(require("lfm.search").mode_search)
lfm.register_mode(require("lfm.search").mode_search_back)

map("/", a(lfm.mode, "search"), { desc = "search" })
map("?", a(lfm.mode, "search-back"), { desc = "search (backwards)" })
map("n", lfm.search_next, { desc = "go to next search result" })
map("N", lfm.search_prev, { desc = "go to previous search result" })

-- Colors
local palette = require("lfm.colors").palette
require("lfm.colors").set({
	broken = { fg = palette.red },
	patterns = {
		{
			color = { fg = palette.magenta },
			ext = {
				".mp3",
				".m4a",
				".ogg",
				".flac",
				".mka",
				".mp4",
				".mkv",
				".m4v",
				".webm",
				".avi",
				".flv",
				".wmv",
				".mov",
				".mpg",
				".mpeg",
				".3gp",
			},
		},
		{
			color = { fg = palette.bright_red },
			ext = {
				".tar",
				".zst",
				".xz",
				".gz",
				".zip",
				".rar",
				".7z",
				".bz2",
			},
		},
		{
			color = { fg = palette.yellow },
			ext = {
				".jpg",
				".jpeg",
				".png",
				".bmp",
				".webp",
				".gif",
			},
		},
	},
})

local function open()
	lfm.eval("open")
end

lfm.register_command("delete", function(a)
	if a then
		error("command takes no arguments")
	end
	lfm.spawn({ "rm", "-rf", "--", unpack(lfm.sel_or_cur()) })
	fm.selection_set()
end, { desc = "Delete current selection without asking for confirmation." })

-- Keymaps

cmap("<Insert>", cmd.toggle_overwrite, { desc = "Toggle insert/overwrite" })
cmap("<Left>", cmd.left, { desc = "Left" })
cmap("<Right>", cmd.right, { desc = "Right" })
cmap("<Home>", cmd.home, { desc = "Home" })
cmap("<End>", cmd._end, { desc = "End" })
cmap("<c-Left>", cmd.word_left, { desc = "jump word left" })
cmap("<c-Right>", cmd.word_right, { desc = "jump word right" })
cmap("<Delete>", cmd.delete_right, { desc = "delete right" })
cmap("<Backspace>", cmd.delete, { desc = "delete left" })
cmap("<c-h>", cmd.delete, { desc = "delete left" })
cmap("<c-w>", cmd.delete_word, { desc = "delete word left" })
cmap("<c-Backspace>", cmd.delete_word, { desc = "delete word left" })
cmap("<c-u>", cmd.delete_line_left, { desc = "delete line left" })
cmap("<Tab>", compl.next, { desc = "next completion item" })
cmap("<s-Tab>", compl.prev, { desc = "previous completion item" })

map("q", lfm.quit, { desc = "quit" })
map("ZZ", lfm.quit, { desc = "quit" })
-- maybe don't write selection/lastdir when exiting with ZQ/:q!
map("ZQ", lfm.quit, { desc = "quit" })
map("<c-q>", lfm.quit, { desc = "quit" })
map("<c-c>", function()
	print("Type :q <Enter> or <Ctrl>q to exit")
end, { desc = "ctrl-c" })
map("<c-l>", ui.clear, { desc = "clear screen and redraw" })
map("<a-r>", fm.drop_cache, { desc = "drop direcory/preview caches" })
map("cd", a(lfm.feedkeys, ":cd "), { desc = ":cd " })
map("<a-c>", fm.check, { desc = "check directories and reload" })

map("&", a(lfm.feedkeys, ":shell-bg "), { desc = ":shell-bg " })
map("s", a(lfm.feedkeys, ":shell "), { desc = ":shell " })
map("S", a(lfm.execute, { "sh", "-c", "LFM_LEVEL=1 " .. os.getenv("SHELL") }), { desc = "open shell" })

-- Visual/selection
map("<Space>", c(fm.selection_toggle, fm.down), { desc = "select current file" })
map("v", fm.selection_reverse, { desc = "reverse selection" })
map("V", fm.visual_toggle, { desc = "toggle visual selection mode" })
map("uv", c(fm.paste_buffer_set, fm.selection_set), { desc = "selection-clear" })

-- Navigation
map("<Enter>", open, { desc = "open" })
map("<Left>", fm.updir, { desc = "go to parent directory" })
map("<Right>", open, { desc = "open file/directory" })
map("j", fm.down, { desc = "move cursor down" })
map("k", fm.up, { desc = "move cursor up" })
map("h", fm.updir, { desc = "go to parent directory" })
map("l", open, { desc = "open file/directory" })
map("L", require("lfm.functions").follow_link)
map("H", a(lfm.feedkeys, "''")) -- complementary to "L"
map("gg", fm.top, { desc = "go to top" })
map("G", fm.bottom, { desc = "go to bottom" })
map("''", fm.jump_automark, { desc = "jump to previous directory" })
map("cd", a(lfm.feedkeys, ":cd "), { desc = ":cd " })
map("<Up>", fm.up, { desc = "move cursor up" })
map("<Down>", fm.down, { desc = "move cursor down" })
map("<c-y>", fm.scroll_up, { desc = "scroll directory up" })
map("<c-e>", fm.scroll_down, { desc = "scroll directory down" })
map("<c-u>", function()
	fm.up(fm.get_height() / 2)
end, { desc = "move cursor half a page up" })
map("<c-d>", function()
	fm.down(fm.get_height() / 2)
end, { desc = "move cursor half a page down" })
map("<c-b>", function()
	fm.up(fm.get_height())
end, { desc = "move cursor half a page up" })
map("<c-f>", function()
	fm.down(fm.get_height())
end, { desc = "move cursor half a page down" })
map("<PageUp>", function()
	fm.up(fm.get_height())
end, { desc = "move cursor half a page up" })
map("<PageDown>", function()
	fm.down(fm.get_height())
end, { desc = "move cursor half a page down" })
map("<Home>", fm.top, { desc = "go to top" })
map("<End>", fm.bottom, { desc = "go to bottom" })

map("zh", function()
	config.hidden = not config.hidden
end, { desc = "toggle hidden files" })

-- Flatten
lfm.register_command(
	"flatten",
	require("lfm.flatten").flatten,
	{ tokenize = true, desc = "(Un)flatten current directory." }
)
map("<a-+>", require("lfm.flatten").flatten_inc, { desc = "increase flatten level" })
map("<a-->", require("lfm.flatten").flatten_dec, { desc = "decrease flatten level" })

-- Find/hinting
-- map("f", a(lfm.mode, "find"), { desc = "find:" })
-- These two only make sense in find: mode. Maybe think about actually providing
-- a way to set keybinds for modes.
-- cmap("<c-n>", lfm.find_next, { mode = "find", desc = "go to next find match" })
-- cmap("<c-p>", lfm.find_prev, { mode = "find", desc = "go to previous find match" })

-- Copy/pasting
map("yn", require("lfm.functions").yank_name, { desc = "yank name" })
map("yp", require("lfm.functions").yank_path, { desc = "yank path" })
map("yy", fm.copy, { desc = "copy" })
map("dd", fm.cut, { desc = "cut" })
map("ud", fm.paste_buffer_clear, { desc = "load-clear" })
map("pp", require("lfm.functions").paste, { desc = "paste-overwrite" })
map("pt", require("lfm.functions").paste_toggle, { desc = "toggle paste mode" })
map("po", require("lfm.functions").paste_overwrite, { desc = "paste-overwrite" })
map("pl", require("lfm.functions").symlink, { desc = "symlink" })
map("pL", require("lfm.functions").symlink_relative, { desc = "symlink-relative" })

-- Renaming
map("cW", a(lfm.feedkeys, ":rename "), { desc = "rename" })
map("cc", a(lfm.feedkeys, ":rename "), { desc = "rename" })
map("cw", require("lfm.functions").rename_until_ext, { desc = "rename-until-ext" })
map("a", require("lfm.functions").rename_before_ext, { desc = "rename-before-ext" })
map("A", require("lfm.functions").rename_after, { desc = "rename-after" })
map("I", require("lfm.functions").rename_before, { desc = "rename-before" })

map("on", a(fm.sortby, "natural", "noreverse"), { desc = "sort: natural, noreverse" })
map("oN", a(fm.sortby, "natural", "reverse"), { desc = "sort: natural, reverse" })
map("os", a(fm.sortby, "size", "reverse"), { desc = "sort: size, noreverse" })
map("oS", a(fm.sortby, "size", "noreverse"), { desc = "sort: size, reverse" })
map("oc", a(fm.sortby, "ctime", "noreverse"), { desc = "sort: ctime, noreverse" })
map("oC", a(fm.sortby, "ctime", "reverse"), { desc = "sort: ctime, reverse" })
map("oa", a(fm.sortby, "atime", "noreverse"), { desc = "sort: atime, noreverse" })
map("oA", a(fm.sortby, "atime", "reverse"), { desc = "sort: atime, reverse" })
map("om", a(fm.sortby, "mtime", "noreverse"), { desc = "sort: mtime, noreverse" })
map("oM", a(fm.sortby, "mtime", "reverse"), { desc = "sort: mtime, reverse" })
map("od", a(fm.sortby, "dirfirst"), { desc = "sort: dirfirst" })
map("oD", a(fm.sortby, "nodirfirst"), { desc = "sort: nodirfirst" })
map("or", a(fm.sortby, "random"), { desc = "sort: random" })

lfm.register_mode(require("lfm.glob").mode_glob_select)
map("*", a(lfm.mode, require("lfm.glob").mode_glob_select.name), { desc = "glob-select" })
lfm.register_command(
	"glob-select",
	require("lfm.glob").glob_select,
	{ tokenize = false, desc = "Select files in the current directory matching a glob." }
)
lfm.register_command(
	"glob-select-rec",
	require("lfm.glob").glob_select_recursive,
	{ tokenize = false, desc = "Select matching a glob recursively." }
)

local function gmap(key, location)
	map("g" .. key, function()
		fm.chdir(location)
	end, { desc = "cd " .. location })
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

return {}
