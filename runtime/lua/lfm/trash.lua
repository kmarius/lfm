local M = {}

local lfm = lfm
local api = lfm.api

-- Trash command, e.g. `{ "trash-put" }`, or `{ "gtrash", "put", "--" }`
---@type string[]?
M.command = nil

local shallow_copy = lfm.util.shallow_copy

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
		lfm.spawn(command, { on_stderr = true })
	end
end

---
---Trash current file or selection, asking for confirmation.
---
---Example:
---```lua
---  lfm.trash.trash_selection()
---```
---
function M.trash_selection()
	if not M.command then
		error("lfm.trash.command not set")
	end
	local files = api.fm_sel_or_cur()
	if #files == 0 then
		error("no files")
	end
	local prompt = ("Trash %d %s [y/N]: "):format(#files, (#files > 1) and "files" or "file")
	lfm.ui.input({ prompt = prompt, single_key = true }, function(input)
		if input == "y" then
			M.put(files)
			api.selection_set({})
		end
	end)
end

return M
