local fm = lfm.fm
local shell = require("shell")

-- local M = require("riclib")
local M = lfm.opener

-- Rename to open_with and match rangers behaviour
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
				lfm.cmd.setline(":", "shell ", ' "${files[@]}"')
			else
				shell.execute(
				{"sh", "-c", match.command, "_", unpack(files)},
				{fork=match.fork, out=false, err=false}
				)
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

function M.ask()
	local file = lfm.sel_or_cur()[1]
	if file then
		local menu = {}
		for _, rule in pairs(M.query(file)) do
			table.insert(menu, rule.number .. " " .. rule.command)
		end
		lfm.cmd.setline(":", "open ", "")
		lfm.ui.menu(menu)
	end
end

local setup = M.setup
function M.setup(t)
	setup(t)
	lfm.register_command("open", M.open, {tokenize = true})
end

return M
