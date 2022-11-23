local lfm = lfm

local fm = lfm.fm
local ui = lfm.ui
local shell = require("lfm.shell")

local M = lfm.rifle

local config

---Navigate into a directory or open files.
---@param ... any
function M.open(...)
	local t = {...}
	local pick = t[1]
	local file = fm.open()
	if file then
		-- selection takes priority
		local files = fm.selection_get()
		if #files == 0 then
			files = {file}
		end
		local match = M.query(files[1], {pick=pick, limit=1})[1]
		if match then
			if match.command == "ask" then
				lfm.cmd.line_set(":", "shell ", ' "${files[@]}"')
			elseif match.term then
				local term = M.query_mime("rifle/x-terminal-emulator", {limit=1})[1]
				if not term then
					error("rifle: no terminal configured"..(config and " in "..config or ""))
				end
				shell.execute({"sh", "-c", term.command, "_", "sh", "-c", match.command, unpack(files)},
				{fork=true, out=false, err=false})
			else
				shell.execute({"sh", "-c", match.command, "_", unpack(files)},
				{fork=match.fork, out=false, err=false})
			end
		else
			if #t > 0 then
				-- assume arguments are a command
				for _, e in pairs(files) do
					table.insert(t, e)
				end
				shell.execute(t)
			else
				print("no matching rules for ".. lfm.fn.mime(files[1]))
			end
		end
	end
end

---Show opener options for the current file in a menu and prepare the command
---line.
function M.ask()
	local file = lfm.sel_or_cur()[1]
	if file then
		local menu = {}
		for _, rule in pairs(M.query(file)) do
			table.insert(menu, rule.number .. " " .. rule.command)
		end
		lfm.cmd.line_set(":", "open ", "")
		ui.menu(menu)
	end
end

-- overwrite the builtin setup
local setup_internal = M.setup

---@class Lfm.Rifle.SetupOpts
---@field config string path to configuration file e.g. a rifle.conf
---@field rules string[] a table of rules as defined in rifle.conf, will take precedence

---Set up opener.
---@param t Lfm.Rifle.SetupOpts
function M.setup(t)
	t = t or {}
	config = t.config
	setup_internal(t)
	lfm.register_command("open", M.open, {tokenize = true})
	lfm.map("r", M.ask, {desc="show opener options"})
end

return M
