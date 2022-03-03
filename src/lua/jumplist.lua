local lfm = lfm

local getpwd = lfm.fn.getpwd
local chdir = lfm.fm.chdir

local M = {}

local list = {getpwd()}
local ind = 1
local registered_hook = false

local function on_chdir()
	local dir = getpwd()
	if list[ind] ~= dir then
		ind = ind + 1
		if ind <= #list then
			if list[ind] ~= dir then
				for i = ind+1,#list do
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
		chdir(list[ind], false)
	end
end

---Jump to the previous location in the jump list.
function M.jump_prev()
	if ind > 1 then
		ind = ind - 1
		chdir(list[ind])
	end
end

---@class jumplist_setup_opts
---@field jump_next_key string default "]"
---@field jump_prev_key string default "["

---Set up jumplist: sets the two keybinds and registers the necessary hook.
---@param t jumplist_setup_opts
function M.setup(t)
	t = t or {}
	if not registered_hook then
		lfm.register_hook("ChdirPost", on_chdir)
		registered_hook = true
	end
	lfm.map(t.jump_next_key or "]", M.jump_next)
	lfm.map(t.jump_prev_key or "[", M.jump_prev)
end

return M
