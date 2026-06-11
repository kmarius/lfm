local tap = require("tap")
local ok = tap.ok
local test = tap.test

lfm.log.level = 0

tap.init(27)

local co = coroutine.running()

test("thread_callback", function()
	lfm.thread("", function(val, err)
		ok(true, "callback is called")
		ok(val == nil, "callback called without value")
		ok(err == nil, "callback called without error")
		coroutine.resume(co)
	end)
	coroutine.yield()
end)

test("thread_callback_optional", function()
	local okay = pcall(function()
		lfm.thread("")
	end)
	ok(okay, "start thread without callback")
end)

test("thread_pass_arg", function()
	lfm.thread("return ({...})[1]", function(val, err)
		ok(val == 7, "value returned")
		ok(err == nil, "value returned without error")
		coroutine.resume(co)
	end, 7)
	coroutine.yield()
end)

test("thread_pass_args", function()
	lfm.thread("return {...}", function(val, err)
		ok(val[1] == 1, "value returned")
		ok(val[2] == 2, "value returned")
		ok(val[3] == 3, "value returned")
		coroutine.resume(co)
	end, 1, 2, 3)
	coroutine.yield()
end)

test("thread_callback_value", function()
	lfm.thread("return 7", function(val, err)
		ok(val == 7, "value returned")
		ok(err == nil, "value returned without error")
		coroutine.resume(co)
	end)
	coroutine.yield()
end)

-- should only pass the first value
test("thread_callback_multiple_values", function()
	lfm.thread("return 7, 8", function(val, err)
		ok(val == 7, "value returned")
		ok(err == nil, "value returned without error")
		coroutine.resume(co)
	end)
	coroutine.yield()
end)

test("thread_error_malformed", function()
	lfm.thread("return (", function(val, err)
		ok(val == nil, "called back without value")
		ok(err ~= nil, "error on malformed")
		coroutine.resume(co)
	end)
	coroutine.yield()
end)

test("thread_error_cfunction", function()
	local okay = pcall(lfm.thread, lfm.quit)
	ok(not okay, "Should not start thread with c function")
end)

test("thread_error_illegal_arg", function()
	local okay = pcall(lfm.thread, "return 7", nil, lfm.quit)
	ok(not okay, "Should not pass cfunction")

	okay = pcall(lfm.thread, "return 7", nil, function() end)
	ok(not okay, "Should not pass function")

	okay = pcall(lfm.thread, "return 7", nil, newproxy(true))
	ok(not okay, "Should not pass userdata")
end)

test("thread_callback_error", function()
	lfm.thread("error('error_value') return 7", function(val, err)
		ok(val == nil, "called back without value")
		-- ideally we can pass the error value as is
		ok(err ~= nil and (err:find("error_value")) > 0, "error message")
		coroutine.resume(co)
	end)
	coroutine.yield()
end)

test("thread_dump", function()
	lfm.thread(function()
		return 7
	end, function(val, err)
		ok(val == 7, "value returned")
		ok(err == nil, "value returned without error")
		coroutine.resume(co)
	end)
	coroutine.yield()
end)

test("thread_dump_with_arg", function()
	lfm.thread(function(n)
		return n + 1
	end, function(val, err)
		ok(val == 7, "value returned")
		ok(err == nil, "value returned without error")
		coroutine.resume(co)
	end, 6)
	coroutine.yield()
end)

test("thread_dump_with_table_arg", function()
	lfm.thread(function(t)
		if type(t) ~= "table" then
			error("not a table, : " .. type(t) .. " size=" .. #t)
		end
		local sum = 0
		for _, e in ipairs(t) do
			sum = sum + e
		end
		return sum
	end, function(val, err)
		ok(val == 10, "value returned")
		ok(err == nil, "value returned without error")
		coroutine.resume(co)
	end, { 1, 2, 3, 4 })
	coroutine.yield()
end)
