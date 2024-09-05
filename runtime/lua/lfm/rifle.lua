local M = lfm.rifle
M._NAME = ...

local lfm = lfm

local fm = lfm.fm
local ui = lfm.ui
local shell = require("lfm.shell")

local config

---Navigate into a directory or open files with rifle.
---```lua
---    lfm.rifle.open()
---```
---Use opener from rifle with label 1 to open the current file/selection.
---```lua
---    lfm.rifle.open(1)
---```
---@param ... any
function M.open(...)
	local t = { ... }
	local pick = t[1]
	local file = fm.open()
	if file then
		-- selection takes priority
		local files = fm.selection_get()
		if #files == 0 then
			files = { file }
		end
		local match = M.query(files[1], { pick = pick, limit = 1 })[1]
		if match then
			if match.command == "ask" then
				lfm.mode("command")
				lfm.cmd.line_set("shell ", ' "${files[@]}"')
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
				lfm.spawn(
					{ "sh", "-c", term.command, "_", "sh", "-c", match.command, unpack(files) },
					{ out = false, err = false }
				)
			else
				if match.fork then
					lfm.spawn({ "sh", "-c", match.command, "_", unpack(files) }, { out = false, err = false })
				else
					lfm.execute({ "sh", "-c", match.command, "_", unpack(files) })
				end
			end
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

---Show opener options for the current file in a menu and prepare the command
---line.
---```lua
---    lfm.rifle.ask()
---```
function M.ask()
	local file = fm.sel_or_cur()[1]
	if file then
		local menu = {}
		for _, rule in pairs(M.query(file)) do
			table.insert(menu, rule.number .. " " .. rule.command)
		end
		lfm.mode("command")
		lfm.cmd.line_set("open ")
		ui.menu(menu)
	end
end

-- overwrite the builtin setup
local setup_internal = M.setup

---@class Lfm.Rifle.SetupOpts
---@field config? string path to configuration file e.g. a rifle.conf
---@field rules? string[] a table of rules as defined in rifle.conf, will take precedence

---Set up rifle.
---```lua
---    lfm.rifle.setup({
---      config = "~/.config/lfm/rifle.conf",
---      rules = {
---        'ext txt, has nvim = nvim "$@"',
---        'ext png, has feh = feh "$@"',
---      },
---    })
---```
---@param opts Lfm.Rifle.SetupOpts
function M.setup(opts)
	opts = opts or {}
	config = opts.config
	setup_internal(opts)
	lfm.register_command("open", M.open, { tokenize = true, desc = "Open file(s)." })
	lfm.map("r", M.ask, { desc = "show opener options" })
end

return M
