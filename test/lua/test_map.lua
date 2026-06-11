local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(4)

local api = lfm.api

test("command_count_0", function()
	api.set_keymap("a", function(c)
		ok(c == nil, "passing 0 to command is nil")
	end)
	api.feedkeys("0a")
end)

test("command_count_1", function()
	api.set_keymap("a", function(c)
		ok(c == 1, "passing 1 to command is 1")
	end)
	api.feedkeys("1a")
end)

test("command_count_2", function()
	api.set_keymap("a", function(c)
		ok(c == 2, "passing 2 to command is 2")
	end)
	api.feedkeys("2a")
end)

test("command_count_1337", function()
	api.set_keymap("a", function(c)
		ok(c == 1337, "passing 1337 to command is 1337")
	end)
	api.feedkeys("1337a")
end)
