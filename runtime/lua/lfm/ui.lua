local M = { _NAME = ... }

local mode = {
	name = "prompt",
	prefix = "",
	input = true,
	on_return = function() end,
	on_esc = function() end,
	on_change = function() end,
}

-- wrap the mode to register so we can dynamically change the callbacks
lfm.register_mode({
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
---@field prompt string
---@field default? string
---@field completion? string
---@field single_key? boolean

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
		local line = lfm.api.cmdline_line_get()
		lfm.mode("normal")
		on_confirm(line)
	end
	if opts.single_key then
		mode.on_change = function()
			local line = lfm.api.cmdline_line_get()
			lfm.mode("normal")
			on_confirm(line)
		end
	else
		mode.on_change = function() end
	end
	lfm.modes[mode.name].prefix = opts.prompt or ""
	lfm.mode(mode.name)
end

return M
