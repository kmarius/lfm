local api = lfm.api
local fm = lfm.fm
local fn = lfm.fn
local log = lfm.log

local execute = lfm.execute
local quit = lfm.quit
local string_format = string.format
local spawn = lfm.spawn
local string_match = string.match
local tokenize = fn.tokenize

-- Enhance logging functions
do
	local table_concat = table.concat
	local level = log.get_level()
	-- Index in the table corresponds to the log level of the function
	for l, name in ipairs({ "trace", "debug", "info", "warn", "error", "fatal" }) do
		local func = log[name]
		log[name] = function(...)
			if l > level then
				local args = { ... }
				local n = select("#", ...)
				for i = 1, n do
					local arg = args[i]
					args[i] = arg == nil and "nil" or tostring(arg)
				end
				func(table_concat(args, "\t"))
			end
		end
		log[name .. "f"] = function(...)
			if l > level then
				func(string_format(...))
			end
		end
	end
	setmetatable(log, {
		__index = function(_, k)
			if k == "level" then
				return level
			end
		end,
		__newindex = function(t, k, v)
			if k == "level" then
				log.set_level(v)
				level = v
			else
				rawset(t, k, v)
			end
		end,
	})
end

---
---Pretty print on the UI (using lfm.inspect). Returns the parameter as is
---
---Example:
---```lua
---  lfm.print({ "Hello", "World" })
---```
---
---@param ... any
function lfm.print(...)
	for _, e in ipairs({ ... }) do
		print(lfm.inspect(e))
	end
	return ...
end

---
---Print a formatted string.
---
---Example:
---```lua
---  lfm.printf("Hello %s", "World")
---```
---
---@param fmt string
---@param ... any
local function printf(fmt, ...)
	print(string_format(fmt, ...))
end
lfm.printf = printf

---
---Print a formatted error.
---
---Example:
---```lua
---  lfm.errorf("errno was %d", errno)
---```
---
---@param fmt string
---@param ... any
local function errorf(fmt, ...)
	lfm.error(string_format(fmt, ...))
end
lfm.errorf = errorf

do -- lfm.validate
	---@param t string|string[]|function
	---@return string
	---@nodiscard
	local function join_types(t)
		if type(t) == "string" then
			return t
		end
		if type(t) == "function" then
			return "-validated by function-"
		end
		return table.concat(t, "|")
	end

	---@param t table
	---@param value any
	---@return boolean
	---@nodiscard
	local function table_contains(t, value)
		for _, v in ipairs(t) do
			if value == v then
				return true
			end
		end
		return false
	end

	---@param name string
	---@param value any
	---@param validator type|type[]|fun(val: any): boolean
	---@param optional? boolean|string
	---@param message? string
	function lfm.validate(name, value, validator, optional, message)
		if not message and type(optional) == "string" then
			message = optional
			optional = false
		end
		if not value then
			if not optional then
				message = message or join_types(validator)
				error(string.format("%s: expected %s, got %s", name, message, "nil"))
			end
			return
		end
		local vtype = type(value)
		if type(validator) == "string" then
			if vtype ~= validator then
				message = message or join_types(validator)
				error(string.format("%s: expected %s, got %s", name, message, vtype))
			end
		elseif type(validator) == "table" then
			if not table_contains(validator, vtype) then
				message = message or join_types(validator)
				error(string.format("%s: expected %s, got %s", name, message, vtype))
			end
		else
			assert(type(validator) == "function")
			if not validator(value) then
				message = message or join_types(validator)
				error(string.format("%s: expected %s, got %s", name, message, tostring(value)))
			end
		end
	end
end

-- lazily load submodules in the lfm namespace, make sure to add them to doc/LuaCATS/lfm.lua
local submodules = {
	complete = true,
	fs = true,
	functions = true,
	inspect = true,
	jumplist = true,
	mode = true,
	quickmarks = true,
	rifle = true,
	search = true,
	shell = true,
	trash = true,
	ui = true,
	util = true,
}

local util = require("lfm.util")
local complete = require("lfm.complete")
local shell = require("lfm.shell")

setmetatable(lfm, {
	__index = function(t, key)
		if submodules[key] then
			t[key] = require("lfm." .. key)
			return t[key]
		end
	end,
})

---@class Lfm.CommandOpts
---@field tokenize? boolean tokenize arguments (default: true)
---@field complete? Lfm.ComplFun completion function
---@field desc? string Description

---@class Lfm.Command : Lfm.CommandOpts
---@field f function corresponding function
---@overload fun(t: string[]): boolean

---@type table<string, Lfm.Command> A table to execute commands, e.g. `lfm.cmd.open()` executes the `open` command.
local commands = setmetatable({}, {
	__call = function(_, line)
		lfm.eval(line)
	end,
})

lfm.cmd = commands

local command_mt = {
	__call = function(self, ...)
		return self.f(...)
	end,
}

local reserved = {
	["and"] = true,
	["break"] = true,
	["do"] = true,
	["elseif"] = true,
	["else"] = true,
	["end"] = true,
	["false"] = true,
	["for"] = true,
	["function"] = true,
	["if"] = true,
	["in"] = true,
	["local"] = true,
	["nil"] = true,
	["not"] = true,
	["or"] = true,
	["repeat"] = true,
	["return"] = true,
	["then"] = true,
	["true"] = true,
	["until"] = true,
	["while"] = true,
}

---
---Register a function as a lfm command or unregister a command. Supported options
---
---Example:
---```lua
---  api.create_command("updir", lfm.fm.updir, { desc = "Go to parent directory" })
---```
---
---Handling arguments:
---```lua
---  api.create_command("cmd", function(line)
---    -- args passed as a single string
---  end, {})
---
---  api.create_command("cmd", function(...)
---    local args = { ... }
---    -- args are split by whitespace
---  end, { tokenize = true })
---```
---
---Using completions (see `complete.lua`):
---```lua
---  api.create_command("cd", chdir, {
---    complete = require("lfm.complete").dirs,
---    tokenize = true,
---  })
---```
---
---@param name string Command name, can not contain whitespace.
---@param f function The function to execute.
---@param opts? Lfm.CommandOpts Additional options.
function lfm.api.create_command(name, f, opts)
	lfm.validate("name", name, "string")
	lfm.validate("name", name, function(val)
		return not reserved[val]
	end, "valid command name")
	lfm.validate("f", f, "function")
	lfm.validate("opts", opts, "table", true)

	opts = util.shallow_copy(opts or {})
	opts.f = f
	opts.tokenize = opts.tokenize == nil and true or opts.tokenize
	opts = setmetatable(opts, command_mt)
	commands[name] = opts --[[@as Lfm.Command]]
	log.tracef("registered command: %s", name)
end

---
---Delete a command.
---
---Example:
---```lua
---  api.del_command("open")
---```
---
---@param name string Command name, can not contain whitespace.
function lfm.api.del_command(name)
	commands[name] = nil
	log.tracef("deleted command: %s", name)
end

---
---Evaluates a line of lua code. If the first whitespace delimited token is a
---registered command it is executed with the following text as arguments.
---Otherwise line is assumed to be lua code and is executed. Results are printed.
---
---Example:
---```lua
---  lfm.eval("cd /home")   -- expression is not lua because "cd" is a registered command
---  lfm.eval('print(2+2)') -- executed as lua code
---```
---
---@param line string
function lfm.eval(line)
	local cmd = string_match(line, "^[^ ]*")
	if not cmd then
		return
	end
	local command = commands[cmd]
	if command then
		local _, args = tokenize(line)
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
		local chunk, err = loadstring(line)
		if not chunk then
			error(err)
		end
		local res = chunk()
		if res then
			print(res)
		end
	end
end

require("lfm.modes")

require("lfm.rifle").setup({
	rules = {
		'mime inode/x-empty, label editor, has $EDITOR = $EDITOR -- "$@"',
		'mime ^text, label editor, has $EDITOR = $EDITOR -- "$@"',
		'mime ^text, label pager, has $PAGER = $PAGER -- "$@"',
	},
})

api.create_command("shell", function(arg)
	shell.bash.execute(arg, { files_via = shell.ARGV })
end, { tokenize = false, complete = complete.files, desc = "Run a shell command." })

api.create_command("shell-bg", function(arg)
	shell.bash.spawn(arg, { files_via = shell.ARGV, on_stdout = true, on_stderr = true })
end, { tokenize = false, complete = complete.files, desc = "Run a shell command in the background." })

require("lfm.jumplist")

-- lfm.glob
api.set_keymap("*", function()
	require("lfm.glob")
	api.mode("glob-select")
end, { desc = "glob-select" })

api.create_command("glob-select", function(arg)
	require("lfm.glob").glob_select(arg)
end, { tokenize = false, desc = "Select files in the current directory matching a glob." })

api.create_command("glob-select-rec", function(arg)
	require("lfm.glob").glob_select_recursive(arg)
end, { tokenize = false, desc = "Select matching a glob recursively." })

-- lfm.quickmarks
api.set_keymap("m", function()
	lfm.quickmarks.prompt_save()
end, { desc = "Save quickmark" })
api.set_keymap("dm", function()
	lfm.quickmarks.prompt_delete()
end, { desc = "Save quickmark" })

api.create_command("quit", quit, { desc = "Quit Lfm" })
api.create_command("q", quit, { desc = "Quit Lfm" })
api.create_command(
	"rename",
	require("lfm.functions").rename,
	{ tokenize = false, complete = complete.limit(1, complete.files), desc = "Rename the current file." }
)

api.create_command("cd", fm.chdir, { tokenize = true, complete = complete.dirs })

api.create_command("startuptime", function()
	require("lfm.profiling").startuptime()
end, { desc = "Display startup profiling info" })

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
	lfm.cmd.open()
end

api.create_command("delete", function(args)
	if args then
		error("command takes no arguments")
	end
	spawn({ "rm", "-rf", "--", unpack(util.selection()) }, { on_stderr = true })
	fm.set_selection()
end, { desc = "Delete current selection without asking for confirmation." })

-- Keymaps
local function cmap(name, f, opts)
	opts.mode = "input"
	api.set_keymap(name, f, opts)
end

cmap("<Insert>", api.cmdline_toggle_overwrite, { desc = "Toggle insert/overwrite" })
cmap("<Left>", api.cmdline_left, { desc = "Left" })
cmap("<Right>", api.cmdline_right, { desc = "Right" })
cmap("<Home>", api.cmdline_home, { desc = "Home" })
cmap("<End>", api.cmdline__end, { desc = "End" })
cmap("<c-Left>", api.cmdline_word_left, { desc = "Jump word left" })
cmap("<C-Right>", api.cmdline_word_right, { desc = "Jump word right" })
cmap("<Delete>", api.cmdline_delete_right, { desc = "Delete right" })
cmap("<Backspace>", api.cmdline_delete, { desc = "Delete left" })
cmap("<c-h>", api.cmdline_delete, { desc = "Delete left" })
cmap("<c-w>", api.cmdline_delete_word, { desc = "Delete word left" })
cmap("<c-Backspace>", api.cmdline_delete_word, { desc = "Delete word left" })
cmap("<c-u>", api.cmdline_delete_line_left, { desc = "Delete line left" })
cmap("<Tab>", complete.next, { desc = "Next completion item" })
cmap("<s-Tab>", complete.prev, { desc = "Previous completion item" })

api.set_keymap("q", quit, { desc = "Quit" })
api.set_keymap("ZZ", quit, { desc = "Quit" })
-- maybe don't write selection/lastdir when exiting with ZQ/:q!
api.set_keymap("ZQ", quit, { desc = "Quit" })
api.set_keymap("<c-q>", quit, { desc = "Quit" })
api.set_keymap("<c-c>", function()
	print("Type :q <Enter> or <Ctrl>q to exit")
end, { desc = "ctrl-c" })
api.set_keymap("<c-l>", api.ui_clear, { desc = "Clear screen and redraw" })
api.set_keymap("<a-r>", fm.drop_cache, { desc = "Drop direcory/preview caches" })
api.set_keymap("cd", a(api.feedkeys, ":cd "), { desc = ":cd " })
api.set_keymap("<a-c>", fm.check, { desc = "Check directories and reload" })

api.set_keymap("&", a(api.feedkeys, ":shell-bg "), { desc = ":shell-bg " })
api.set_keymap("s", a(api.feedkeys, ":shell "), { desc = ":shell " })
api.set_keymap("S", a(execute, { "sh", "-c", "LFM_LEVEL=1 " .. os.getenv("SHELL") }), { desc = "Open a $SHELL" })

-- Visual/selection
api.set_keymap("<Space>", c(fm.toggle_selection, fm.down), { desc = "Select current file" })
api.set_keymap("v", fm.reverse_selection, { desc = "Reverse selection" })
api.set_keymap("V", function()
	local mode = api.current_mode()
	api.mode(mode ~= "visual" and "visual" or "normal")
end, { desc = "Toggle visual selection mode" })
api.set_keymap("uv", c(fm.set_paste_buffer, fm.set_selection), { desc = "Clear selection" })
api.set_keymap("gu", fm.restore_selection, { desc = "Restore previous selection" })

-- Navigation
api.set_keymap("<Enter>", open, { desc = "Open file or directory" })
api.set_keymap("<Left>", fm.updir, { desc = "Go to parent directory" })
api.set_keymap("<Right>", open, { desc = "Open file/directory" })
api.set_keymap("j", fm.down, { desc = "Move cursor down" })
api.set_keymap("k", fm.up, { desc = "Move cursor up" })
api.set_keymap("h", fm.updir, { desc = "Go to parent directory" })
api.set_keymap("l", open, { desc = "Open file/directory" })
api.set_keymap("L", require("lfm.functions").follow_link, { desc = "Follow symlink under cursor" })
api.set_keymap("H", a(api.feedkeys, "''")) -- complementary to "L"
api.set_keymap("gg", fm.top, { desc = "Go to top" })
api.set_keymap("G", fm.bottom, { desc = "Go to bottom" })
api.set_keymap("''", fm.jump_automark, { desc = "Jump to previous directory" })
api.set_keymap("cd", a(api.feedkeys, ":cd "), { desc = ":cd " })
api.set_keymap("<Up>", fm.up, { desc = "Move cursor up" })
api.set_keymap("<Down>", fm.down, { desc = "Move cursor down" })
api.set_keymap("<c-y>", fm.scroll_up, { desc = "Scroll directory up" })
api.set_keymap("<c-e>", fm.scroll_down, { desc = "Scroll directory down" })
api.set_keymap("<c-u>", function()
	fm.up(fm.get_height() / 2)
end, { desc = "Move cursor half a page up" })
api.set_keymap("<c-d>", function()
	fm.down(fm.get_height() / 2)
end, { desc = "Move cursor half a page down" })
api.set_keymap("<c-b>", function()
	fm.up(fm.get_height())
end, { desc = "Move cursor half a page up" })
api.set_keymap("<c-f>", function()
	fm.down(fm.get_height())
end, { desc = "Move cursor half a page down" })
api.set_keymap("<PageUp>", function()
	fm.up(fm.get_height())
end, { desc = "Move cursor half a page up" })
api.set_keymap("<PageDown>", function()
	fm.down(fm.get_height())
end, { desc = "Move cursor half a page down" })
api.set_keymap("<Home>", fm.top, { desc = "Go to top" })
api.set_keymap("<End>", fm.bottom, { desc = "Go to bottom" })

api.set_keymap("zh", function()
	lfm.o.hidden = not lfm.o.hidden
end, { desc = "Toggle hidden files" })

-- lfm.trash
api.set_keymap("df", function()
	require("lfm.trash").trash_selection()
end, { desc = "Trash current file or selection" })

-- lfm.flatten
api.create_command("flatten", function(l)
	require("lfm.flatten").flatten(l)
end, { tokenize = true, desc = "(Un)flatten current directory." })
api.set_keymap("<a-+>", function()
	require("lfm.flatten").increment()
end, { desc = "Increase flatten level" })
api.set_keymap("<a-->", function()
	require("lfm.flatten").decrement()
end, { desc = "Decrease flatten level" })

-- Copy/pasting
api.set_keymap("yn", require("lfm.functions").yank_name, { desc = "Yank name" })
api.set_keymap("yp", require("lfm.functions").yank_path, { desc = "Yank path" })
api.set_keymap("yy", fm.copy, { desc = "copy" })
api.set_keymap("dd", fm.cut, { desc = "cut" })
api.set_keymap("ud", function()
	fm.set_paste_buffer({})
end, { desc = "Clear paste buffer" })
api.set_keymap("pp", require("lfm.functions").paste, { desc = "Paste files" })
api.set_keymap("pt", require("lfm.functions").toggle_paste, { desc = "Toggle paste mode" })
api.set_keymap("po", require("lfm.functions").paste_overwrite, { desc = "Paste files with overwrite" })
api.set_keymap("pl", require("lfm.functions").symlink, { desc = "Create symlink" })
api.set_keymap("pL", require("lfm.functions").symlink_relative, { desc = "Create relative symlink" })

-- Renaming
api.set_keymap("cW", a(api.feedkeys, ":rename "), { desc = "Rename" })
api.set_keymap("cc", a(api.feedkeys, ":rename "), { desc = "Rename" })
api.set_keymap("cw", require("lfm.functions").rename_until_ext, { desc = "Rename until extension" })
api.set_keymap("a", require("lfm.functions").rename_before_ext, { desc = "Rename before extension" })
api.set_keymap("A", require("lfm.functions").rename_after, { desc = "Rename at the end" })
api.set_keymap("I", require("lfm.functions").rename_before, { desc = "Rename at the start" })

-- TODO: change these when more file info values are implemented
api.set_keymap("on", function()
	fm.sort({ type = "natural", reverse = false })
	fm.set_info("size")
end, { desc = "Sort: natural, noreverse" })
api.set_keymap("oN", function()
	fm.sort({ type = "natural", reverse = true })
	fm.set_info("size")
end, { desc = "Sort: natural, reverse" })
api.set_keymap("os", function()
	fm.sort({ type = "size", reverse = true })
	fm.set_info("size")
end, { desc = "Sort: size, noreverse" })
api.set_keymap("oS", function()
	fm.sort({ type = "size", reverse = false })
	fm.set_info("size")
end, { desc = "Sort: size, reverse" })
api.set_keymap("oc", function()
	fm.sort({ type = "ctime", reverse = true })
	fm.set_info("ctime")
end, { desc = "Sort: ctime, reverse" })
api.set_keymap("oC", function()
	fm.sort({ type = "ctime", reverse = false })
	fm.set_info("ctime")
end, { desc = "Sort: ctime, noreverse" })
api.set_keymap("oa", function()
	fm.sort({ type = "atime", reverse = true })
	fm.set_info("atime")
end, { desc = "Sort: atime, reverse" })
api.set_keymap("oA", function()
	fm.sort({ type = "atime", reverse = false })
	fm.set_info("atime")
end, { desc = "Sort: atime, noreverse" })
api.set_keymap("om", function()
	fm.sort({ type = "mtime", reverse = true })
	fm.set_info("mtime")
end, { desc = "Sort: mtime, reverse" })
api.set_keymap("oM", function()
	fm.sort({ type = "mtime", reverse = false })
	fm.set_info("mtime")
end, { desc = "Sort: mtime, noreverse" })
api.set_keymap("or", function()
	fm.sort({ type = "random" })
	fm.set_info("size")
end, { desc = "Sort: random" })

for key, loc in pairs({
	["e"] = "/etc",
	["h"] = os.getenv("HOME"),
	["m"] = "/mnt",
	["n"] = "~/Downloads",
	["o"] = "/opt",
	["p"] = "/tmp",
	["r"] = "/",
	["s"] = "/srv",
}) do
	api.set_keymap("g" .. key, function()
		fm.chdir(loc)
	end, { desc = "Go to " .. loc })
end
