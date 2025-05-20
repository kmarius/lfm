-- Set up package.path to include ~/.config/lfm/lua and remove ./
local paths = lfm.paths

package.path = string.gsub(package.path, "%./%?.lua;", "")
package.path = package.path .. ";" .. paths.config_dir .. "/lua/?.lua;" .. paths.config_dir .. "/lua/?/init.lua"
package.cpath = string.gsub(package.cpath, "%./%?.so;", "")

local fn = lfm.fn
local api = lfm.api
local log = lfm.log

local cmap = lfm.cmap
local execute = lfm.execute
local handle_key = lfm.handle_key
local lfm_error = lfm.error
local map = lfm.map
local quit = lfm.quit
local spawn = lfm.spawn
local string_format = string.format
local string_match = string.match
local table_concat = table.concat
local tokenize = fn.tokenize

-- Enhance logging functions
do
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

---Pretty print on the UI. Returns the parameter as is
---```lua
---    lfm.print({ "Hello", "World" })
---```
---@param ... any
function lfm.print(...)
	for _, e in ipairs({ ... }) do
		print(lfm.inspect(e))
	end
	return ...
end

---Print a formatted string.
---```lua
---    lfm.printf("Hello %s", "World")
---```
---@param fmt string
---@param ... any
local function printf(fmt, ...)
	print(string_format(fmt, ...))
end

---Print a formatted error.
---```lua
---    lfm.errorf("errno was %d", errno)
---```
---@param fmt string
---@param ... any
local function errorf(fmt, ...)
	lfm_error(string_format(fmt, ...))
end

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
		message = message or join_types(validator)
		if not value then
			if not optional then
				error(name .. ": expected " .. message .. ", got nil")
			else
				return
			end
		end
		local vtype = type(value)
		if type(validator) == "string" then
			if vtype ~= validator then
				error(name .. ": expected " .. message .. ", got " .. vtype)
			end
		elseif type(validator) == "table" then
			if not table_contains(validator, vtype) then
				error(name .. ": expected " .. message .. ", got " .. vtype)
			end
		else
			assert(type(validator) == "function")
			if not validator(value) then
				error(name .. ": expected " .. message .. ", got " .. tostring(value))
			end
		end
	end
end

---Get the current selection or file under the cursor.
---```lua
---    local files = lfm.api.fm_sel_or_cur()
---    for i, file in ipairs(files) do
---      print(i, file)
---    end
---```
---@return string[] selection
local function sel_or_cur()
	local sel = api.fm_selection_get()
	return #sel > 0 and sel or { api.fm_current_file() }
end

---Feed keys into the key handler.
---```lua
---    lfm.feedkeys("cd", "<Enter>")
---```
---@param ... string
local function feedkeys(...)
	for _, seq in ipairs({ ... }) do
		handle_key(seq)
	end
end

---@class Lapi.fm_CommandOpts
---@field tokenize? boolean tokenize arguments (default: true)
---@field compl? Lapi.fm_ComplFun completion function
---@field desc? string Description

---@class Lapi.fm_Command : Lapi.fm_CommandOpts
---@field f function corresponding function
---@overload fun(t: string[]): boolean

---@type table<string, Lapi.fm_Command>
local commands = setmetatable({}, {
	__call = function(_, line)
		lfm.eval(line)
	end,
})
lfm.commands = commands

local command_mt = {
	__call = function(self, arg)
		if arg then
			self.f(unpack(arg))
		else
			self.f()
		end
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

---Register a function as a lfm command or unregister a command. Supported options
---```lua
---    lfm.register_command("updir", api.fm_updir, { desc = "Go to parent directory" })
---```
---Handling arguments:
---```lua
---    lfm.register_command("cmd", function(line)
---      -- args passed as a single string
---    end, {})
---
---    lfm.register_command("cmd", function(...)
---      local args = { ... }
---      -- args are split by whitespace
---    end, { tokenize = true })
---```
---Using completions (see `compl.lua`):
---```lua
---    lfm.register_command("cd", chdir, {
---      compl = require("lfm.compl").dirs,
---      tokenize = true,
---    })
---```
---@param name string Command name, can not contain whitespace.
---@param f function The function to execute or `nil` to unregister
---@param opts? Lapi.fm_CommandOpts Additional options.
local function register_command(name, f, opts)
	if reserved[name] then
		error("reserved command name: " .. name)
	end

	-- TODO: we should probably make a copy of opts
	if f then
		opts = opts or {}
		---@diagnostic disable-next-line: inject-field
		opts.f = f
		opts.tokenize = opts.tokenize == nil and true or opts.tokenize
		opts = setmetatable(opts, command_mt)
		lfm.commands[name] = opts --[[@as Lapi.fm_Command]]
	else
		lfm.commands[name] = nil
	end
end

---Evaluates a line of lua code. If the first whitespace delimited token is a
---registered command it is executed with the following text as arguments.
---Otherwise line is assumed to be lua code and is executed. Results are printed.
---```lua
---    lfm.eval("cd /home")   -- expression is not lua because "cd" is a registered command
---    lfm.eval('print(2+2)') -- executed as lua code
---```
---@param line string
local function eval(line)
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

lfm.printf = printf
lfm.errorf = errorf
lfm.feedkeys = feedkeys
lfm.eval = eval
lfm.register_command = register_command
lfm.api.fm_sel_or_cur = sel_or_cur

-- lazily load submodules in the lfm namespace, make sure to add them to doc/LuaCATS/lfm.lua
local submodules = {
	compl = true,
	fs = true,
	functions = true,
	inspect = true,
	jumplist = true,
	macros = true,
	mode = true,
	quickmarks = true,
	rifle = true,
	search = true,
	shell = true,
	trash = true,
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

register_command("shell", function(arg)
	shell.bash.execute(arg, { files_via = shell.ARGV })
end, { tokenize = false, compl = compl.files, desc = "Run a shell command." })

register_command("shell-bg", function(arg)
	shell.bash.spawn(arg, { files_via = shell.ARGV, stdout = true, stderr = true })
end, { tokenize = false, compl = compl.files, desc = "Run a shell command in the background." })

require("lfm.jumplist")._setup()
require("lfm.quickmarks")._setup()
require("lfm.macros")._setup()
require("lfm.glob")._setup()

register_command("quit", quit, { desc = "Quit Lapi.fm_" })
register_command("q", quit, { desc = "Quit Lapi.fm_" })
register_command(
	"rename",
	require("lfm.functions").rename,
	{ tokenize = false, compl = compl.limit(1, compl.files), desc = "Rename the current file." }
)

register_command("cd", api.fm_chdir, { tokenize = true, compl = compl.dirs })

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
	eval("open")
end

register_command("delete", function(args)
	if args then
		error("command takes no arguments")
	end
	spawn({ "rm", "-rf", "--", unpack(sel_or_cur()) }, { stderr = true })
	api.fm_selection_set()
end, { desc = "Delete current selection without asking for confirmation." })

-- Keymaps

cmap("<Insert>", api.cmdline_toggle_overwrite, { desc = "Toggle insert/overwrite" })
cmap("<Left>", api.cmdline_left, { desc = "Left" })
cmap("<Right>", api.cmdline_right, { desc = "Right" })
cmap("<Home>", api.cmdline_home, { desc = "Home" })
cmap("<End>", api.cmdline__end, { desc = "End" })
cmap("<c-Left>", api.cmdline_word_left, { desc = "Jump word left" })
cmap("<c-Right>", api.cmdline_word_right, { desc = "Jump word right" })
cmap("<Delete>", api.cmdline_delete_right, { desc = "Delete right" })
cmap("<Backspace>", api.cmdline_delete, { desc = "Delete left" })
cmap("<c-h>", api.cmdline_delete, { desc = "Delete left" })
cmap("<c-w>", api.cmdline_delete_word, { desc = "Delete word left" })
cmap("<c-Backspace>", api.cmdline_delete_word, { desc = "Delete word left" })
cmap("<c-u>", api.cmdline_delete_line_left, { desc = "Delete line left" })
cmap("<Tab>", compl.next, { desc = "Next completion item" })
cmap("<s-Tab>", compl.prev, { desc = "Previous completion item" })

map("q", quit, { desc = "Quit" })
map("ZZ", quit, { desc = "Quit" })
-- maybe don't write selection/lastdir when exiting with ZQ/:q!
map("ZQ", quit, { desc = "Quit" })
map("<c-q>", quit, { desc = "Quit" })
map("<c-c>", function()
	print("Type :q <Enter> or <Ctrl>q to exit")
end, { desc = "ctrl-c" })
map("<c-l>", api.ui_clear, { desc = "Clear screen and redraw" })
map("<a-r>", api.fm_drop_cache, { desc = "Drop direcory/preview caches" })
map("cd", a(feedkeys, ":cd "), { desc = ":cd " })
map("<a-c>", api.fm_check, { desc = "Check directories and reload" })

map("&", a(feedkeys, ":shell-bg "), { desc = ":shell-bg " })
map("s", a(feedkeys, ":shell "), { desc = ":shell " })
map("S", a(execute, { "sh", "-c", "LFM_LEVEL=1 " .. os.getenv("SHELL") }), { desc = "Open a $SHELL" })

-- Visual/selection
map("<Space>", c(api.fm_selection_toggle, api.fm_down), { desc = "Select current file" })
map("v", api.fm_selection_reverse, { desc = "Reverse selection" })
map("V", function()
	local mode = lfm.current_mode()
	lfm.mode(mode ~= "visual" and "visual" or "normal")
end, { desc = "Toggle visual selection mode" })
map("uv", c(api.fm_paste_buffer_set, api.fm_selection_set), { desc = "Clear selection" })
map("gu", api.fm_selection_restore, { desc = "Restore previous selection" })

-- Navigation
map("<Enter>", open, { desc = "Open file or directory" })
map("<Left>", api.fm_updir, { desc = "Go to parent directory" })
map("<Right>", open, { desc = "Open file/directory" })
map("j", api.fm_down, { desc = "Move cursor down" })
map("k", api.fm_up, { desc = "Move cursor up" })
map("h", api.fm_updir, { desc = "Go to parent directory" })
map("l", open, { desc = "Open file/directory" })
map("L", require("lfm.functions").follow_link, { desc = "Follow symlink under cursor" })
map("H", a(feedkeys, "''")) -- complementary to "L"
map("gg", api.fm_top, { desc = "Go to top" })
map("G", api.fm_bottom, { desc = "Go to bottom" })
map("''", api.fm_jump_automark, { desc = "Jump to previous directory" })
map("cd", a(feedkeys, ":cd "), { desc = ":cd " })
map("<Up>", api.fm_up, { desc = "Move cursor up" })
map("<Down>", api.fm_down, { desc = "Move cursor down" })
map("<c-y>", api.fm_scroll_up, { desc = "Scroll directory up" })
map("<c-e>", api.fm_scroll_down, { desc = "Scroll directory down" })
map("<c-u>", function()
	api.fm_up(api.fm_get_height() / 2)
end, { desc = "Move cursor half a page up" })
map("<c-d>", function()
	api.fm_down(api.fm_get_height() / 2)
end, { desc = "Move cursor half a page down" })
map("<c-b>", function()
	api.fm_up(api.fm_get_height())
end, { desc = "Move cursor half a page up" })
map("<c-f>", function()
	api.fm_down(api.fm_get_height())
end, { desc = "Move cursor half a page down" })
map("<PageUp>", function()
	api.fm_up(api.fm_get_height())
end, { desc = "Move cursor half a page up" })
map("<PageDown>", function()
	api.fm_down(api.fm_get_height())
end, { desc = "Move cursor half a page down" })
map("<Home>", api.fm_top, { desc = "Go to top" })
map("<End>", api.fm_bottom, { desc = "Go to bottom" })

map("zh", function()
	lfm.o.hidden = not lfm.o.hidden
end, { desc = "Toggle hidden files" })

-- Flatten
register_command(
	"flatten",
	require("lfm.flatten").flatten,
	{ tokenize = true, desc = "(Un)flatten current directory." }
)
map("<a-+>", require("lfm.flatten").increment, { desc = "Increase flatten level" })
map("<a-->", require("lfm.flatten").decrement, { desc = "Decrease flatten level" })

-- Copy/pasting
map("yn", require("lfm.functions").yank_name, { desc = "Yank name" })
map("yp", require("lfm.functions").yank_path, { desc = "Yank path" })
map("yy", api.fm_copy, { desc = "copy" })
map("dd", api.fm_cut, { desc = "cut" })
map("ud", function()
	api.fm_paste_buffer_set({})
end, { desc = "Clear paste buffer" })
map("pp", require("lfm.functions").paste, { desc = "Paste files" })
map("pt", require("lfm.functions").toggle_paste, { desc = "Toggle paste mode" })
map("po", require("lfm.functions").paste_overwrite, { desc = "Paste files with overwrite" })
map("pl", require("lfm.functions").symlink, { desc = "Create symlink" })
map("pL", require("lfm.functions").symlink_relative, { desc = "Create relative symlink" })

-- Renaming
map("cW", a(feedkeys, ":rename "), { desc = "Rename" })
map("cc", a(feedkeys, ":rename "), { desc = "Rename" })
map("cw", require("lfm.functions").rename_until_ext, { desc = "Rename until extension" })
map("a", require("lfm.functions").rename_before_ext, { desc = "Rename before extension" })
map("A", require("lfm.functions").rename_after, { desc = "Rename at the end" })
map("I", require("lfm.functions").rename_before, { desc = "Rename at the start" })

-- TODO: change these when more file info values are implemented
local sort = api.fm_sort
local set_info = api.fm_set_info
map("on", function()
	sort({ type = "natural", reverse = false })
	set_info("size")
end, { desc = "Sort: natural, noreverse" })
map("oN", function()
	sort({ type = "natural", reverse = true })
	set_info("size")
end, { desc = "Sort: natural, reverse" })
map("os", function()
	sort({ type = "size", reverse = true })
	set_info("size")
end, { desc = "Sort: size, noreverse" })
map("oS", function()
	sort({ type = "size", reverse = false })
	set_info("size")
end, { desc = "Sort: size, reverse" })
map("oc", function()
	sort({ type = "ctime", reverse = true })
	set_info("ctime")
end, { desc = "Sort: ctime, reverse" })
map("oC", function()
	sort({ type = "ctime", reverse = false })
	set_info("ctime")
end, { desc = "Sort: ctime, noreverse" })
map("oa", function()
	sort({ type = "atime", reverse = true })
	set_info("atime")
end, { desc = "Sort: atime, reverse" })
map("oA", function()
	sort({ type = "atime", reverse = false })
	set_info("atime")
end, { desc = "Sort: atime, noreverse" })
map("om", function()
	sort({ type = "mtime", reverse = true })
	set_info("mtime")
end, { desc = "Sort: mtime, reverse" })
map("oM", function()
	sort({ type = "mtime", reverse = false })
	set_info("mtime")
end, { desc = "Sort: mtime, noreverse" })
map("or", function()
	sort({ type = "random" })
	set_info("size")
end, { desc = "Sort: random" })

local function gmap(key, location)
	local chdir = api.fm_chdir
	map("g" .. key, function()
		chdir(location)
	end, { desc = "Go to " .. location })
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
