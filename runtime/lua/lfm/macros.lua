local M = { _NAME = ... }

local lfm = lfm
local is_recording = lfm.api.ui_macro_recording
local macro_record = lfm.api.ui_macro_record
local macro_stop_record = lfm.api.ui_macro_stop_record
local macro_play = lfm.api.ui_macro_play
local line_get = lfm.api.cmdline_line_get

local record_mode = {
	name = "macro-record",
	input = true,
	prefix = "macro-record: ",
	on_return = function()
		lfm.mode("normal")
	end,
	on_change = function()
		local id = line_get()
		lfm.mode("normal")
		macro_record(id)
	end,
}

local prev -- identifier of the last run macro
local count = 1 -- repetition count, set before entering replay mode

local play_mode = {
	name = "macro-play",
	input = true,
	prefix = "macro-play: ",
	on_return = function()
		lfm.mode("normal")
	end,
	on_change = function()
		local id = line_get()
		-- "W" replays the last played macro
		if id == "W" and prev then
			id = prev
		else
			prev = id
		end
		-- macro is played in the next iteration in the main loop
		for _ = 1, count do
			lfm.schedule(function()
				macro_play(id)
			end)
		end
		lfm.mode("normal")
	end,
}

function M._setup()
	lfm.register_mode(record_mode)
	lfm.register_mode(play_mode)
	lfm.map("w", function()
		if is_recording() then
			macro_stop_record()
		else
			lfm.mode(record_mode.name)
		end
	end, { desc = "Start or stop recording a macro" })
	lfm.map("W", function(ct)
		count = ct or 1
		lfm.mode(play_mode.name)
	end, { desc = "Replay a macro" })
end

return M
