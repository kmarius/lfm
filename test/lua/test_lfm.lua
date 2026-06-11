local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(4)

local co = coroutine.running()

test("schedule_immediately", function()
	lfm.schedule(function()
		ok(true, "scheduled function called immediately")
		coroutine.resume(co)
	end, 0)
	coroutine.yield()
end)

test("schedule_negative", function()
	lfm.schedule(function()
		ok(true, "scheduled function called immediately")
		coroutine.resume(co)
	end, -100)
	coroutine.yield()
end)

test("schedule_delayed", function()
	lfm.schedule(function()
		ok(true, "scheduled function called with delay")
		coroutine.resume(co)
	end, 100)
	coroutine.yield()
end)

test("eval", function()
	lfm.eval("a = 2")
	ok(a == 2, "can evaluate an  expression")
	_G.a = nil
end)
