-- documentation moved to docs/LuaCATS/lfm/rifle.lua

local M = lfm.rifle
M._NAME = ...

local lfm = lfm

local api = lfm.api
local util = lfm.util

local config

---@class Lfm.Rifle.QueryOpts
---@field pick string|number Choose the match with the label pick, match number n.
---@field limit integer Limit the number of results.

---@class Lfm.Rifle.Match
---@field command string Command to execute
---@field fork boolean Should the command be forked
---@field lfm boolean Should the command be evaluated in lfm
---@field term boolean Should the command be run in a terminal
---@field esc boolean Number of this match
---@field number integer Number of this match

---@class Lfm.Rifle.FileInfo
---@field path string Path to the file
---@field name string File name
---@field mime string mimeype

---
---Query rifle for options how to run/open a file.
---
---```lua
---  local matches = rifle.query("file.txt")
---  local match = matches[1]
---  if match then
---    lfm.rifle.exec(match)
---  end
---```
---
---```lua
---  -- return the first match with label "editor"
---  local matches = rifle.query("file.txt", { pick = "editor", limit = 1 })
---  local match = matches[1]
---  if match then
---    lfm.rifle.exec(match)
---  end
---```
---
---@param path string
---@param opts? Lfm.Rifle.QueryOpts
---@return Lfm.Rifle.Match[]
function M.query(path, opts) end

---
---Query rifle for options how to run/open files with the given mimetype.
---
---```lua
---  local matches = rifle.query("application/pdf")
---  local match = matches[1]
---  if match and match.fork then
---    lfm.spawn({'sh', '-c', match.command, "some.pdf"})
---  end
---```
---@param mime string
---@param opts? Lfm.Rifle.QueryOpts
---@return Lfm.Rifle.Match[]
function M.query_mime(mime, opts) end

---
---Execute a rifle match according to its flags.
---
---```lua
---  local matches = rifle.query("file.txt")
---  local match = matches[1]
---  if match then
---    rifle.exec(match, { "file.txt", "file2.txt" })
---  end
---```
---
---@param match Lfm.Rifle.Match
---@param files string[]
function M.exec(match, files)
	if match.command == "ask" then
		api.mode("command")
		api.cmdline_line_set("shell ", ' "${files[@]}"')
	elseif match.lfm then
		local f, err = loadstring(match.command)
		if not f then
			error(err)
		end
		local res = f(unpack(files))
		if res then
			if type(res) == "table" then
				res = lfm.inspect(res)
			end
			print(res)
		end
	elseif match.term then
		local term = M.query_mime("rifle/x-terminal-emulator", { limit = 1 })[1]
		if not term then
			error("rifle: no terminal configured" .. (config and " in " .. config or ""))
		end
		lfm.spawn({ "sh", "-c", term.command, "_", "sh", "-c", match.command, unpack(files) })
	else
		if match.fork then
			lfm.spawn({ "sh", "-c", match.command, "_", unpack(files) })
		else
			lfm.execute({ "sh", "-c", match.command, "_", unpack(files) })
		end
	end
end

---
---Navigate into a directory or open files with rifle.
---
---Example:
---```lua
---  rifle.open()
---```
---
---Use opener from rifle with label 1 to open the current file/selection:
---```lua
---  rifle.open(1)
---```
---
---@param ... string|number
function M.open(...)
	local t = { ... }
	local pick = t[1]
	local file = api.fm_open()
	if file then
		-- selection takes priority
		local files = api.selection_get()
		if #files == 0 then
			files = { file }
		end
		local match = M.query(files[1], { pick = pick, limit = 1 })[1]
		if match then
			M.exec(match, files)
		else
			if #t > 0 then
				-- assume arguments are a command
				for _, e in pairs(files) do
					table.insert(t, e)
				end
				lfm.execute(t)
			else
				print("no matching rules for " .. lfm.fn.mime(files[1]))
			end
		end
	end
end

---
---Show opener options for the current file in a menu and prepare the command
---line.
---
---Example:
---```lua
---  rifle.ask()
---```
---
function M.ask()
	local file = util.selection()
	if file[1] then
		local menu = {}
		for _, rule in pairs(M.query(file[1])) do
			table.insert(menu, rule.number .. " " .. rule.command)
		end
		api.mode("command")
		api.cmdline_line_set("open ")
		api.ui_menu(menu)
	end
end

---
---Get the information rifle uses to determine a match.
---
---```lua
---  local info = rifle.fileinfo("file.txt")
---  print(info.path)
---  print(info.file)
---  print(info.mime)
---```
---@param path string
---@return Lfm.Rifle.FileInfo
function M.fileinfo(path) end

---@class Lfm.Rifle.SetupOpts
---@field config? string path to configuration file e.g. a rifle.conf
---@field rules? string[] a table of rules as defined in rifle.conf, will take precedence

---
---Set up rifle.
---
---Example:
---```lua
---  lfm.rifle.setup({
---    config = "~/.config/lfm/rifle.conf",
---    rules = {
---      'ext txt, has nvim = nvim "$@"',
---      'ext png, has feh = feh "$@"',
---    },
---  })
---```
---
---@param opts Lfm.Rifle.SetupOpts
function M.setup(opts)
	opts = opts or {}
	config = opts.config
	M._setup(opts)
	api.create_command("open", M.open, { tokenize = true, desc = "Open file(s)." })
	api.set_keymap("r", M.ask, { desc = "show opener options" })
end

-- replace stubs with implementation to trick luals
for _, v in ipairs({ "fileinfo", "nrules", "query", "query_mime" }) do
	M[v] = M["_" .. v]
end

return M
