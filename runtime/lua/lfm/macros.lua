local M = { _NAME = ... }

local lfm = lfm
local api = lfm.api
local ui = lfm.ui

local prev -- identifier of the last run macro

lfm.map("w", function()
	if api.macro_recording() then
		api.macro_stop_record()
	else
		ui.input({ prompt = "macro-record: ", single_key = true }, function(id)
			if id and id ~= "" then
				lfm.mode("normal")
				api.macro_record(id)
			end
		end)
	end
end, { desc = "Start or stop recording a macro" })

lfm.map("W", function(ct)
	local count = ct or 1
	ui.input({ prompt = "macro-play: ", single_key = true }, function(id)
		if id and id ~= "" then
			if id == "W" and prev then
				id = prev
			else
				prev = id
			end
			for _ = 1, count do
				lfm.schedule(function()
					api.macro_play(id)
				end)
			end
		end
	end)
end, { desc = "Replay a macro" })

return M
