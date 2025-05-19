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

lfm.map("df", function()
	if not M.command then
		error("lfm.trash.command not set")
	end
	local files = api.fm_sel_or_cur()
	if #files == 0 then
		error("no files")
	end
	local prompt = ("Trash %d %s [y/N]: "):format(#files, (#files > 1) and "files" or "file")
	lfm.util.input({ prompt = prompt, single_key = true }, function(input)
		if input == "y" then
			M.put(files)
			api.fm_selection_set({})
		end
	end)
end, { desc = "Trash file/selection" })

return M
