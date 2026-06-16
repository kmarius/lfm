local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(19)

local api = lfm.api

-- wrap set_keymap so we can unmap in cleanup
local maps = {}
local function set_keymap(lhs, rhs, opts)
	api.set_keymap(lhs, rhs, opts)
	maps[lhs] = opts or {}
end

local function cleanup()
	for lhs, opts in pairs(maps) do
		api.del_keymap(lhs, opts)
	end
	maps = {}
	api.feedkeys("<esc>")
	api.mode("normal")
end

-- test modes
api.create_mode({
	name = "normal-test-mode",
})

-- overwrite on_return in a test to access the line
local on_return
api.create_mode({
	name = "input-test-mode",
	prefix = "input-test-mode: ",
	input = true,
	on_return = function(line)
		on_return(line)
	end,
})

test("basic_map", function()
	local called = false
	set_keymap("a", function()
		called = true
	end)
	api.feedkeys("a")
	ok(called, "mapping was called")
end, cleanup)

test("long_map", function()
	local called = false
	set_keymap("defghij", function()
		called = true
	end)
	api.feedkeys("defghi")
	ok(not called, "mapping was not called")
	api.feedkeys("j")
	ok(called, "mapping was called")
end, cleanup)

test("map_shadows", function()
	local called = 0

	set_keymap("bc", function()
		called = called + 1
	end)
	api.feedkeys("bc")
	ok(called == 1, "mapping was called")

	set_keymap("b", function()
		called = called + 1
	end)
	set_keymap("c", function()
		called = called + 1
	end)
	api.feedkeys("bc")
	ok(called == 3, "mapping was shadowed")
end, cleanup)

test("can_unmap", function()
	local called = false
	set_keymap("a", function()
		called = true
	end, { mode = "normal-test-mode" })
	api.del_keymap("a", { mode = "normal-test-mode" })
	api.feedkeys("a")
	ok(not called, "mapping was not called")
end, cleanup)

test("can_unmap_in_mode", function()
	local called = false
	set_keymap("a", function()
		called = true
	end, { mode = "normal-test-mode" })
	api.del_keymap("a", { mode = "normal-test-mode" })
	api.mode("normal-test-mode")
	api.feedkeys("a")
	ok(not called, "mode mapping was not called")
end, cleanup)

test("can_unmap_in_input_mode", function()
	local called = false
	api.set_keymap("a", function()
		called = true
	end, { mode = "input-test-mode" })
	api.del_keymap("a", { mode = "input-test-mode" })
	api.mode("input-test-mode")
	api.feedkeys("a")
	ok(not called, "input mapping was not called")
end, cleanup)

test("command_count_0", function()
	set_keymap("a", function(c)
		ok(c == nil, "passing 0 to command is nil")
	end)
	api.feedkeys("0a")
end, cleanup)

test("command_count_1", function()
	set_keymap("a", function(c)
		ok(c == 1, "passing 1 to command is 1")
	end)
	api.feedkeys("1a")
end, cleanup)

test("command_count_2", function()
	set_keymap("a", function(c)
		ok(c == 2, "passing 2 to command is 2")
	end)
	api.feedkeys("2a")
end, cleanup)

test("command_count_1337", function()
	set_keymap("a", function(c)
		ok(c == 1337, "passing 1337 to command is 1337")
	end)
	api.feedkeys("1337a")
end, cleanup)

test("basic_input", function()
	api.feedkeys("<esc>")
	api.mode("input-test-mode")
	local res
	on_return = function(line)
		res = line
	end
	api.feedkeys("abc<enter>")
	ok(res == "abc", "input was correct")
end, cleanup)

test("input_map_printable", function()
	api.feedkeys("<esc>")
	api.mode("input-test-mode")
	local res
	on_return = function(line)
		res = line
	end
	api.feedkeys("abc<enter>")
	ok(res == "abc", "input was correct")
end, cleanup)

test("input_with_map", function()
	api.mode("input-test-mode")
	set_keymap("<c-d>", function()
		api.feedkeys("000")
	end, { mode = "input-test-mode" })
	local res
	on_return = function(line)
		res = line
	end
	api.feedkeys("abc<c-d>def<enter>")
	ok(res == "abc000def", "input was correct")
end, cleanup)

test("input_map_with_printable", function()
	api.mode("input-test-mode")
	set_keymap("a", function()
		api.feedkeys("000")
	end, { mode = "input-test-mode" })
	local res
	on_return = function(line)
		res = line
	end
	api.feedkeys("a<enter>")
	ok(res == "000", "input was correct")
end, cleanup)

test("input_with_map_only_prefix_is_mapped", function()
	api.mode("input-test-mode")
	set_keymap("<c-b>z", function()
		api.feedkeys("000")
	end, { mode = "input-test-mode" })
	local res
	on_return = function(line)
		res = line
	end

	api.feedkeys("abc<c-b>ydef<enter>")
	ok(res == "abcdef", "input was correct")
end, cleanup)

test("mode_mape_takes_precedence", function()
	local base_called = false
	local mode_called = false

	set_keymap("a", function()
		mode_called = true
	end, { mode = "normal-test-mode" })

	set_keymap("a", function()
		base_called = true
	end, { mode = "normal" })

	api.mode("normal-test-mode")
	api.feedkeys("a")
	ok(mode_called and not base_called, "mode mapping called")

	base_called, mode_called = false, false
	api.mode("normal")
	api.feedkeys("a")
	ok(not mode_called and base_called, "base mapping called")
end, cleanup)
