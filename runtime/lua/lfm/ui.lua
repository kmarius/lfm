local M = { _NAME = ... }

local lfm = lfm
local api = lfm.api

local mode = {
	name = "prompt",
	prefix = "",
	input = true,
	on_return = function() end,
	on_esc = function() end,
	on_change = function() end,
}

-- we create a wrapped mode so we can change the callbacks later
api.create_mode({
	name = mode.name,
	prefix = mode.prefix,
	input = mode.input,
	on_return = function()
		mode.on_return()
	end,
	on_esc = function()
		mode.on_esc()
	end,
	on_change = function()
		mode.on_change()
	end,
})

---@class Lfm.Ui.InputOpts
---@field prompt string default: `""`
---@field default? string unused
---@field completion? string unused
---@field single_key? boolean Accept a single key of input without requiring Enter.

---
---Prompt for input.
---
---Example:
---```lua
---  ui.input({ prompt = "Say something: " }, function(input)
---    if input then
---      print("Input was: " .. input)
---    end
---  end)
---```
---
---@param opts Lfm.Ui.InputOpts
---@param on_confirm fun(input: string?)
function M.input(opts, on_confirm)
	lfm.validate("opts", opts, "table")
	lfm.validate("on_confirm", on_confirm, "function")

	mode.on_esc = on_confirm
	mode.on_return = function()
		local line = api.cmdline_line_get()
		api.mode("normal")
		on_confirm(line)
	end
	if opts.single_key then
		mode.on_change = function()
			local line = api.cmdline_line_get()
			api.mode("normal")
			on_confirm(line)
		end
	else
		mode.on_change = function() end
	end
	api.update_mode(mode.name, { prefix = opts.prompt or "" })
	api.mode(mode.name)
end

return M
