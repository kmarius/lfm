local M = { _NAME = ... }

local lfm = lfm

local getpwd = lfm.fn.getpwd
local chdir = lfm.fm.chdir

local list = { getpwd() }
local ind = 1

local function on_chdir()
	local dir = getpwd()
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

---Jump to the next location in the jump list.
---```lua
---    lfm.jumplist.jump_next()
---```
function M.jump_next()
	if ind < #list then
		ind = ind + 1
		chdir(list[ind])
	end
end

---Jump to the previous location in the jump list.
---```lua
---    lfm.jumplist.jump_prev()
---```
function M.jump_prev()
	if ind > 1 then
		ind = ind - 1
		chdir(list[ind])
	end
end

---Set up jumplist: sets keybinds and registers the necessary hook.
function M._setup()
	lfm.register_hook("ChdirPost", on_chdir)
	lfm.map("]", M.jump_next, { desc = "jumplist-next" })
	lfm.map("[", M.jump_prev, { desc = "jumplist-prev" })
end

return M
