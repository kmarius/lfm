local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(1)

local co = coroutine.running()

test("fifo_called_back", function()
	Callback = function(n)
		ok(n == 7, "fifo is read")
		coroutine.resume(co)
	end
	lfm.spawn({ "sh", "-c", "echo 'Callback(7)'>$LFMFIFO" })
	coroutine.yield()
end)
