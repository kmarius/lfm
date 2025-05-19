local M = {}

local lfm = lfm
local api = lfm.api

-- Trash command, e.g. `{ "trash-put" }`, or `{ "gtrash", "put", "--" }`
---@type string[]?
M.command = nil

local function shallow_copy(t)
	local copy = {}
	for i = 1, #t do
		copy[i] = t[i]
	end
	return copy
end

---
---Trash files.
---
---Example:
---```lua
---  lfm.trash.put({ "/path/to/file.jpg" })
---```
---
---@param files string[]
function M.put(files)
	if not M.command then
		error("lfm.trash.command not set")
	end
	if #files > 0 then
		local command = shallow_copy(M.command)
		for i, file in ipairs(files) do
			command[i + #M.command] = file
		end
		lfm.spawn(command, { stderr = true })
	end
end

local trash_mode = {
	name = "delete",
	input = true,
	prefix = "delete [y/N]: ",
	on_return = function()
		lfm.mode("normal")
	end,
	on_change = function()
		local line = api.cmdline_line_get()
		lfm.mode("normal")
		if line == "y" then
			M.put(api.fm_sel_or_cur())
			api.fm_selection_set({})
		end
	end,
}

lfm.register_mode(trash_mode)

lfm.map("df", function()
	if not M.command then
		error("lfm.trash.command not set")
	end
	lfm.mode("delete")
end, { desc = "Trash file/selection" })

return M
