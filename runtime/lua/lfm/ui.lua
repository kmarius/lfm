local M = { _NAME = ... }

local lfm = lfm
local api = lfm.api

local MODE_NAME = "prompt"

api.create_mode({
	name = MODE_NAME,
	input = true,
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

	local upd = {
		prefix = opts.prompt or "",
		on_change = false,
		on_esc = on_confirm,
		on_return = function()
			local line = api.cmdline_line_get()
			api.mode("normal")
			on_confirm(line)
		end,
	}

	if opts.single_key then
		upd.on_change = function()
			local line = api.cmdline_line_get()
			api.mode("normal")
			on_confirm(line)
		end
	end

	api.update_mode(MODE_NAME, upd)
	api.mode(MODE_NAME)
end

return M
