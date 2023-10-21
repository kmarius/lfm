-- DON'T use local lfm = lfm here because it brakes sumneko.

local config = lfm.config

package.path = string.gsub(package.path, "%./%?.lua;", "")
package.path = package.path .. ";" .. config.configdir .. "/lua/?.lua;" .. config.configdir .. "/lua/?/init.lua"
package.cpath = string.gsub(package.cpath, "%./%?.so;", "")

local fm = lfm.fm
local log = lfm.log
local ui = lfm.ui
local cmd = lfm.cmd

-- enhance logging functions
do
	local string_format = string.format
	local newlog = {}
	for name, func in pairs(log) do
		if type(func) == "function" then
			newlog[name] = function(...)
				local t = { ... }
				for i, e in pairs(t) do
					t[i] = tostring(e)
				end
				func(table.concat(t, " "))
			end
			newlog[name .. "f"] = function(...)
				func(string_format(...))
			end
		end
	end
	for name, func in pairs(newlog) do
		log[name] = func
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
			error(err)
		end
	end
end

-- Commands
lfm.commands = {}

---@class Lfm.CommandOpts
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
---@param t? Lfm.CommandOpts Additional options.
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

-- lazily load submodules in the lfm namespace, make sure to add them to doc/EmmyLua/lfm.lua
local submodules = {
	compl = true,
	functions = true,
	inspect = true,
	jumplist = true,
	mode = true,
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

require("lfm.modes")._setup()

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
	shell.bash(arg, { files_via = shell.ARRAY })()
end, { tokenize = false, compl = compl.files, desc = "Run a shell command." })

lfm.register_command("shell-bg", function(arg)
	shell.bash(arg, { files_via = shell.ARRAY, fork = true })()
end, { tokenize = false, compl = compl.files, desc = "Run a shell command in the background." })

require("lfm.jumplist")._setup()
require("lfm.quickmarks")._setup()
require("lfm.glob")._setup()

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
cmap("<c-Left>", cmd.word_left, { desc = "Jump word left" })
cmap("<c-Right>", cmd.word_right, { desc = "Jump word right" })
cmap("<Delete>", cmd.delete_right, { desc = "Delete right" })
cmap("<Backspace>", cmd.delete, { desc = "Delete left" })
cmap("<c-h>", cmd.delete, { desc = "Delete left" })
cmap("<c-w>", cmd.delete_word, { desc = "Delete word left" })
cmap("<c-Backspace>", cmd.delete_word, { desc = "Delete word left" })
cmap("<c-u>", cmd.delete_line_left, { desc = "Delete line left" })
cmap("<Tab>", compl.next, { desc = "Next completion item" })
cmap("<s-Tab>", compl.prev, { desc = "Previous completion item" })

map("q", lfm.quit, { desc = "Quit" })
map("ZZ", lfm.quit, { desc = "Quit" })
-- maybe don't write selection/lastdir when exiting with ZQ/:q!
map("ZQ", lfm.quit, { desc = "Quit" })
map("<c-q>", lfm.quit, { desc = "Quit" })
map("<c-c>", function()
	print("Type :q <Enter> or <Ctrl>q to exit")
end, { desc = "ctrl-c" })
map("<c-l>", ui.clear, { desc = "Clear screen and redraw" })
map("<a-r>", fm.drop_cache, { desc = "Drop direcory/preview caches" })
map("cd", a(lfm.feedkeys, ":cd "), { desc = ":cd " })
map("<a-c>", fm.check, { desc = "Check directories and reload" })

map("&", a(lfm.feedkeys, ":shell-bg "), { desc = ":shell-bg " })
map("s", a(lfm.feedkeys, ":shell "), { desc = ":shell " })
map("S", a(lfm.execute, { "sh", "-c", "LFM_LEVEL=1 " .. os.getenv("SHELL") }), { desc = "Open a $SHELL" })

-- Visual/selection
map("<Space>", c(fm.selection_toggle, fm.down), { desc = "Select current file" })
map("v", fm.selection_reverse, { desc = "Reverse selection" })
map("V", fm.visual_toggle, { desc = "Toggle visual selection mode" })
map("uv", c(fm.paste_buffer_set, fm.selection_set), { desc = "Clear selection" })

-- Navigation
map("<Enter>", open, { desc = "Open file or directory" })
map("<Left>", fm.updir, { desc = "Go to parent directory" })
map("<Right>", open, { desc = "Open file/directory" })
map("j", fm.down, { desc = "Move cursor down" })
map("k", fm.up, { desc = "Move cursor up" })
map("h", fm.updir, { desc = "Go to parent directory" })
map("l", open, { desc = "Open file/directory" })
map("L", require("lfm.functions").follow_link, { desc = "Follow symlink under cursor" })
map("H", a(lfm.feedkeys, "''")) -- complementary to "L"
map("gg", fm.top, { desc = "Go to top" })
map("G", fm.bottom, { desc = "Go to bottom" })
map("''", fm.jump_automark, { desc = "Jump to previous directory" })
map("cd", a(lfm.feedkeys, ":cd "), { desc = ":cd " })
map("<Up>", fm.up, { desc = "Move cursor up" })
map("<Down>", fm.down, { desc = "Move cursor down" })
map("<c-y>", fm.scroll_up, { desc = "Scroll directory up" })
map("<c-e>", fm.scroll_down, { desc = "Scroll directory down" })
map("<c-u>", function()
	fm.up(fm.get_height() / 2)
end, { desc = "Move cursor half a page up" })
map("<c-d>", function()
	fm.down(fm.get_height() / 2)
end, { desc = "Move cursor half a page down" })
map("<c-b>", function()
	fm.up(fm.get_height())
end, { desc = "Move cursor half a page up" })
map("<c-f>", function()
	fm.down(fm.get_height())
end, { desc = "Move cursor half a page down" })
map("<PageUp>", function()
	fm.up(fm.get_height())
end, { desc = "Move cursor half a page up" })
map("<PageDown>", function()
	fm.down(fm.get_height())
end, { desc = "Move cursor half a page down" })
map("<Home>", fm.top, { desc = "Go to top" })
map("<End>", fm.bottom, { desc = "Go to bottom" })

map("zh", function()
	config.hidden = not config.hidden
end, { desc = "Toggle hidden files" })

-- Flatten
lfm.register_command(
	"flatten",
	require("lfm.flatten").flatten,
	{ tokenize = true, desc = "(Un)flatten current directory." }
)
map("<a-+>", require("lfm.flatten").flatten_inc, { desc = "Increase flatten level" })
map("<a-->", require("lfm.flatten").flatten_dec, { desc = "Decrease flatten level" })

-- Copy/pasting
map("yn", require("lfm.functions").yank_name, { desc = "Yank name" })
map("yp", require("lfm.functions").yank_path, { desc = "Yank path" })
map("yy", fm.copy, { desc = "copy" })
map("dd", fm.cut, { desc = "cut" })
map("ud", fm.paste_buffer_clear, { desc = "Clear paste buffer" })
map("pp", require("lfm.functions").paste, { desc = "Paste files" })
map("pt", require("lfm.functions").paste_toggle, { desc = "Toggle paste mode" })
map("po", require("lfm.functions").paste_overwrite, { desc = "Paste files with overwrite" })
map("pl", require("lfm.functions").symlink, { desc = "Create symlink" })
map("pL", require("lfm.functions").symlink_relative, { desc = "Create relative symlink" })

-- Renaming
map("cW", a(lfm.feedkeys, ":rename "), { desc = "Rename" })
map("cc", a(lfm.feedkeys, ":rename "), { desc = "Rename" })
map("cw", require("lfm.functions").rename_until_ext, { desc = "Rename until extension" })
map("a", require("lfm.functions").rename_before_ext, { desc = "Rename before extension" })
map("A", require("lfm.functions").rename_after, { desc = "Rename at the end" })
map("I", require("lfm.functions").rename_before, { desc = "Rename at the start" })

-- TODO: change these when more file info values are implemented
local sortby = fm.sortby
local set_info = fm.set_info
map("on", function()
	sortby("natural", "noreverse")
	set_info("size")
end, { desc = "Sort: natural, noreverse" })
map("oN", function()
	sortby("natural", "reverse")
	set_info("size")
end, { desc = "Sort: natural, reverse" })
map("os", function()
	sortby("size", "reverse")
	set_info("size")
end, { desc = "Sort: size, noreverse" })
map("oS", function()
	sortby("size", "noreverse")
	set_info("size")
end, { desc = "Sort: size, reverse" })
map("oc", function()
	sortby("ctime", "noreverse")
	set_info("ctime")
end, { desc = "Sort: ctime, noreverse" })
map("oC", function()
	sortby("ctime", "reverse")
	set_info("ctime")
end, { desc = "Sort: ctime, reverse" })
map("oa", function()
	sortby("atime", "noreverse")
	set_info("atime")
end, { desc = "Sort: atime, noreverse" })
map("oA", function()
	sortby("atime", "reverse")
	set_info("atime")
end, { desc = "Sort: atime, reverse" })
map("om", function()
	sortby("mtime", "noreverse")
	set_info("mtime")
end, { desc = "Sort: mtime, noreverse" })
map("oM", function()
	sortby("mtime", "reverse")
	set_info("mtime")
end, { desc = "Sort: mtime, reverse" })
map("or", function()
	sortby("random")
	set_info("size")
end, { desc = "Sort: random" })

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
