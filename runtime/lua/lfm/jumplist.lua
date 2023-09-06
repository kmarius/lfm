local lfm = lfm

local getpwd = lfm.fn.getpwd
local chdir = lfm.fm.chdir

local M = {}

local list = { getpwd() }
local ind = 1
local registered_hook = false

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
function M.jump_next()
	if ind < #list then
		ind = ind + 1
		chdir(list[ind])
	end
end

---Jump to the previous location in the jump list.
function M.jump_prev()
	if ind > 1 then
		ind = ind - 1
		chdir(list[ind])
	end
end

---@class Lfm.JumpList.SetupOpts
---@field key_next string default "]"
---@field key_prev string default "["

---Set up jumplist: sets the two keybinds and registers the necessary hook.
---@param t? Lfm.JumpList.SetupOpts
function M._setup(t)
	t = t or {}
	if not registered_hook then
		lfm.register_hook("ChdirPost", on_chdir)
		registered_hook = true
	end
	lfm.map(t.key_next or "]", M.jump_next, { desc = "jumplist-next" })
	lfm.map(t.key_prev or "[", M.jump_prev, { desc = "jumplist-prev" })
end

return M
