local M = { _NAME = ... }

local lfm = lfm

local getpwd = lfm.fn.getpwd
local chdir = lfm.api.chdir

local list = { getpwd() }
local ind = 1

local disable_hook = false

local function on_chdir(dir)
	if disable_hook then
		disable_hook = false
		return
	end
	if list[ind] ~= dir then
		ind = ind + 1
		if ind <= #list then
			if list[ind] ~= dir then
				for i = ind + 1, #list do
					list[i] = nil
				end
			end
		end
		list[ind] = dir
	end
end

---
---Get the current jump list and current index.
---
---Example:
---```lua
---  local list, ind = lfm.jumplist.get_list()
---```
---
---@return table
---@return integer
function M.get_list()
	local ls = {}
	for i, loc in ipairs(list) do
		ls[i] = loc
	end
	return ls, ind
end

---
---Jump to a location in the jump list, by index.
---
---Example:
---```lua
---  lfm.jumplist.jump_to(7)
---```
---
---@param idx integer
function M.jump_to(idx)
	if idx >= 1 and idx <= #list then
		ind = idx
		disable_hook = true
		chdir(list[ind])
	end
end

---
---Jump to the next location in the jump list.
---
---Example:
---```lua
---  lfm.jumplist.jump_next()
---```
---
function M.jump_next()
	if ind < #list then
		ind = ind + 1
		chdir(list[ind])
	end
end

---
---Jump to the previous location in the jump list.
---
---Example:
---```lua
---  lfm.jumplist.jump_prev()
---```
---
function M.jump_prev()
	if ind > 1 then
		ind = ind - 1
		chdir(list[ind])
	end
end

---Set up jumplist: sets keybinds and registers the necessary hook.
function M._setup()
	lfm.register_hook("ChdirPost", on_chdir)
	lfm.api.set_keymap("]", M.jump_next, { desc = "jumplist-next" })
	lfm.api.set_keymap("[", M.jump_prev, { desc = "jumplist-prev" })
end

return M
