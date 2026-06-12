local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(13)

local api = lfm.api

test("basic_map", function()
	local called = false
	api.set_keymap("a", function()
		called = true
	end)
	api.feedkeys("a")
	ok(called, "mapping was called")

	api.del_keymap("a")
end)

test("long_map", function()
	local called = false
	api.set_keymap("defghij", function()
		called = true
	end)
	api.feedkeys("defghi")
	ok(not called, "mapping was not called")
	api.feedkeys("j")
	ok(called, "mapping was called")

	api.del_keymap("defghij")
end)

test("map_shadows", function()
	local called = 0

	api.set_keymap("bc", function()
		called = called + 1
	end)
	api.feedkeys("bc")
	ok(called == 1, "mapping was called")

	api.set_keymap("b", function()
		called = called + 1
	end)
	api.set_keymap("c", function()
		called = called + 1
	end)
	api.feedkeys("bc")
	ok(called == 3, "mapping was shadowed")

	api.del_keymap("b")
	api.del_keymap("c")
	api.del_keymap("bc")
end)

test("can_unmap", function()
	local called = false
	api.set_keymap("a", function()
		called = true
	end)
	api.del_keymap("a")
	api.feedkeys("a<esc>")
	ok(not called, "mapping was not called")
end)

test("command_count_0", function()
	api.set_keymap("a", function(c)
		ok(c == nil, "passing 0 to command is nil")
	end)
	api.feedkeys("0a")

	api.del_keymap("a")
end)

test("command_count_1", function()
	api.set_keymap("a", function(c)
		ok(c == 1, "passing 1 to command is 1")
	end)
	api.feedkeys("1a")

	api.del_keymap("a")
end)

test("command_count_2", function()
	api.set_keymap("a", function(c)
		ok(c == 2, "passing 2 to command is 2")
	end)
	api.feedkeys("2a")

	api.del_keymap("a")
end)

test("command_count_1337", function()
	api.set_keymap("a", function(c)
		ok(c == 1337, "passing 1337 to command is 1337")
	end)
	api.feedkeys("1337a")

	api.del_keymap("a")
end)

local on_return
api.create_mode({
	name = "input-test",
	prefix = "input-test: ",
	input = true,
	on_return = function(line)
		on_return(line)
	end,
})

test("basic_input", function()
	api.feedkeys("<esc>")
	api.mode("input-test")
	local res
	on_return = function(line)
		res = line
	end
	api.feedkeys("abc<enter>")
	ok(res == "abc", "input was correct")
end)

test("input_with_map", function()
	api.feedkeys("<esc>")
	api.mode("input-test")
	api.set_keymap("<c-d>", function()
		api.feedkeys("000")
	end, { mode = "input-test" })
	local res
	on_return = function(line)
		res = line
	end
	api.feedkeys("abc<c-d>def<enter>")
	ok(res == "abc000def", "input was correct")
	api.del_keymap("<c-d>", { mode = "input-test" })
end)

test("input_with_map_with_printable", function()
	api.feedkeys("<esc>")
	api.mode("input-test")
	-- TODO: it seems we currently can't set a keymap for a specific input mode
	-- so <c-a> is not recognized in input-test mode
	api.set_keymap("<c-a>zz", function()
		api.feedkeys("000")
	end, { mode = "input" })
	local res
	on_return = function(line)
		res = line
	end
	api.feedkeys("abc<c-a>zzdef<enter>")
	ok(res == "abc000def", "input was correct")
	api.del_keymap("<c-a>zz", { mode = "input-test" })
end)
