local lfm = lfm

local fm = lfm.fm
local ui = lfm.ui
local shell = require("shell")

local M = lfm.rifle

---Navigate into a directory or open files.
---@param ... any
---@return boolean
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
			else
				shell.execute({"sh", "-c", match.command, "_", unpack(files)},
				{fork=match.fork, out=false, err=false})
			end
			return true
		else
			-- assume arguments are a command
			for _, e in pairs(files) do
				table.insert(t, e)
			end
			shell.execute(t)
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
local setup = M.setup

---@class rifle_setup_opts
---@field config string path to configuration file e.g. a rifle.conf (default: ~/.config/lfm/opener.conf)

---Set up opener.
---@param t rifle_setup_opts
function M.setup(t)
	setup(t)
	lfm.register_command("open", M.open, {tokenize = true})
	lfm.map("r", M.ask, {desc="show opener options"})
end

return M
