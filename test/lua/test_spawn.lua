local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(22)

local co = coroutine.running()

test("spawn_exit_status_0", function()
	local resumed = false
	lfm.spawn({ "sleep", "0.1" }, {
		on_exit = function(r)
			ok(true, "called back on exit")
			ok(r == 0, "exit status is 0")
			resumed = true
			coroutine.resume(co)
		end,
	})
	lfm.schedule(function()
		if not resumed then
			ok(false, "called back on exit")
			ok(false, "exit status is 0")
			coroutine.resume(co)
		end
	end, 500)
	coroutine.yield()
end)

test("spawn_exit_status_1", function()
	local resumed = false
	lfm.spawn({ "sh", "-c", "exit 1" }, {
		on_exit = function(r)
			ok(true, "called back on exit")
			ok(r == 1, "exit status is 1")
			resumed = true
			coroutine.resume(co)
		end,
	})
	lfm.schedule(function()
		if not resumed then
			ok(false, "called back on exit")
			ok(false, "exit status is 1")
			coroutine.resume(co)
		end
	end, 500)
	coroutine.yield()
end)

test("spawn_capture_stdout", function()
	lfm.spawn({ "printf", "abc" }, {
		on_stdout = function(line)
			ok(line == "abc", "stdout captured")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

test("spawn_capture_stdout_long", function()
	-- fails with 2^18, because both pipes connected to cat will fill
	local input = string.rep("_", math.pow(2, 17))
	lfm.spawn({ "cat" }, {
		stdin = input,
		on_stdout = function(line)
			ok(line == input, "stdout captured")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

test("spawn_capture_capture_long_table", function()
	-- fails with 2^18, because both pipes connected to cat will fill
	local input = string.rep("_", math.pow(2, 17))
	lfm.spawn({ "cat" }, {
		stdin = { input },
		on_stdout = function(line)
			ok(line == input, "stdout captured")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

test("spawn_capture_stdout_with_nul", function()
	lfm.spawn({ "printf", "abc\\0def" }, {
		on_stdout = function(line)
			ok(line == "abc\0def", "stdout with nul captured")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

test("spawn_capture_stderr", function()
	lfm.spawn({ "sh", "-c", "echo abc >&2" }, {
		on_stderr = function(line)
			ok(line == "abc", "stderr captured")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

test("spawn_capture_stderr_with_nul", function()
	lfm.spawn({ "sh", "-c", "echo 'abc\\0def' >&2" }, {
		on_stderr = function(line)
			ok(line == "abc\0def", "stderr captured")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

test("spawn_stdin", function()
	lfm.spawn({ "cat" }, {
		stdin = "abc",
		on_stdout = function(line)
			ok(line == "abc", "stdin passed to child")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

test("spawn_stdin_with_nul", function()
	lfm.spawn({ "cat" }, {
		stdin = "abc\0def",
		on_stdout = function(line)
			ok(line == "abc\0def", "stdin with nul passed to child")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

test("spawn_stdin_multiple_lines", function()
	local stdin = { "line1", "line2" }
	local out = {}
	lfm.spawn({ "cat" }, {
		stdin = stdin,
		on_stdout = function(line)
			out[#out + 1] = line
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
	ok(#stdin == #out and stdin[1] == out[1] and stdin[2] == out[2], "stdin passed to child")
end)

test("spawn_stdin_function", function()
	local proc = lfm.spawn({ "cat" }, {
		stdin = true,
		on_stdout = function(line)
			coroutine.resume(co, line)
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	})
	ok(proc, "spawn successful")
	assert(proc)
	proc:write("abc")
	ok(coroutine.yield(), "abc", "stdin passed to child")
	proc:close()
	coroutine.yield()
end)

test("spawn_stdin_function_with_nul", function()
	local res = assert(lfm.spawn({ "cat" }, {
		stdin = true,
		on_stdout = function(line)
			ok(line == "abc\0def", "stdin passed to child")
		end,
		on_exit = function()
			coroutine.resume(co)
		end,
	}))
	res:write("abc\0def")
	res:close()
	coroutine.yield()
end)

test("spawn_stdin_function2", function()
	local res = lfm.spawn({ "cat" }, {
		stdin = true,
		on_stdout = function(line)
			coroutine.resume(co, line)
		end,
		on_exit = function(ret)
			coroutine.resume(co, ret)
		end,
	})
	ok(res, "spawn successful")
	assert(res)

	res:write("abc")
	ok(coroutine.yield() == "abc", "receive expected output")

	res:write("def")
	ok(coroutine.yield() == "def", "receive expected output")

	res:close()
	ok(coroutine.yield() == 0, "expected return status")
end)

test("spawn_env", function()
	lfm.spawn({ "sh", "-c", 'test "$TESTVAR" = TESTVAL' }, {
		on_exit = function(ret)
			ok(ret == 0, "environment variable passed")
			coroutine.resume(co)
		end,
		env = { TESTVAR = "TESTVAL" },
	})
	coroutine.yield()
end)

test("spawn_output_before_exit", function()
	local stdout_called_back = false
	lfm.spawn({ "echo", "hi" }, {
		on_stdout = function()
			stdout_called_back = true
		end,
		on_exit = function()
			ok(stdout_called_back, "stdout captured before exit")
			coroutine.resume(co)
		end,
	})
	coroutine.yield()
end)

-- if _G["_Lfm_tests_enabled"] then
-- 	local assert_eq = require("assert").assert_eq
-- 	local assert_true = require("assert").assert_true
-- 	local assert_false = require("assert").assert_false
--
-- 	assert_true(M.exists("/"))
-- 	assert_false(M.exists("/surely/not"))
--
-- 	assert_eq(M.abspath("/abc/def"), "/abc/def")
-- 	assert_eq(M.abspath("abc/def"), lfm.fn.getcwd() .. "/abc/def")
-- 	assert_eq(M.abspath("~"), "/home/marius")
-- 	assert_eq(M.abspath("~/abc/def"), "/home/marius/abc/def")
--
-- 	assert_eq(M.basename("abc"), "abc")
-- 	assert_eq(M.basename("/abc/def"), "def")
-- 	assert_eq(M.basename("./abc/def"), "def")
--
-- 	assert_eq(M.dirname("/a/b"), "/a")
-- 	assert_eq(M.dirname("/a/b/"), "/a/b")
-- 	assert_eq(M.dirname("a/b"), "a")
-- 	assert_eq(M.dirname("a/b/"), "a/b")
-- 	assert_eq(M.dirname("/a"), "/")
-- 	assert_eq(M.dirname("a"), ".")
-- 	assert_eq(M.dirname("."), ".")
-- 	assert_eq(M.dirname("/"), "/")
-- 	assert_eq(M.dirname("/"), "/")
--
-- 	assert_eq(M.joinpath("/abc", "def.txt"), "/abc/def.txt")
-- 	assert_eq(M.joinpath("foo/", "/bar"), "foo/bar")
-- 	assert_eq(M.joinpath("~", "Desktop", "files"), "~/Desktop/files")
--
-- 	assert_eq(M.normalize("."), ".")
-- 	assert_eq(M.normalize("./"), ".")
-- 	assert_eq(M.normalize("././"), ".")
-- 	assert_eq(M.normalize("./././"), ".")
-- 	assert_eq(M.normalize("$HOME"), "/home/marius")
-- 	assert_eq(M.normalize("$FUGG"), "$FUGG")
-- 	assert_eq(M.normalize("$HOME/abc"), "/home/marius/abc")
-- 	assert_eq(M.normalize("~"), "/home/marius")
-- 	assert_eq(M.normalize("~/"), "/home/marius")
-- 	assert_eq(M.normalize("~/abc"), "/home/marius/abc")
-- 	assert_eq(M.normalize("/home///marius"), "/home/marius")
-- 	assert_eq(M.normalize("/home/marius/../"), "/home")
-- 	assert_eq(M.normalize("/home/marius/.."), "/home")
-- 	assert_eq(M.normalize("/home/marius/../marius"), "/home/marius")
-- 	assert_eq(M.normalize("/home/marius/../../home"), "/home")
-- 	assert_eq(M.normalize("/../../home"), "/home")
-- 	assert_eq(M.normalize("../../abc"), "../../abc")
--
-- 	assert_eq(M.normalize("~/src/nvim/api/../tui/./tui.c"), "/home/marius/src/nvim/tui/tui.c")
-- 	assert_eq(M.normalize("~/src/neovim"), "/home/marius/src/neovim")
-- 	assert_eq(M.normalize("$XDG_CONFIG_HOME/nvim/init.vim"), "/home/marius/.config/nvim/init.vim")
-- 	assert_eq(M.normalize("./foo/bar"), "foo/bar")
-- 	assert_eq(M.normalize("foo/../../../bar"), "../../bar")
-- 	assert_eq(M.normalize("/home/jdoe/../../../bar"), "/bar")
--
-- 	assert_eq(M.relpath("/var", "/var/lib"), "lib")
-- 	assert_eq(M.relpath("/var", "/usr/bin"), nil)
--
-- 	local function test_parents(path, expected)
-- 		local parents = {}
-- 		for p in M.parents(path) do
-- 			table.insert(parents, p)
-- 		end
-- 		assert_eq(#parents, #expected, 2)
-- 		for i, p in ipairs(parents) do
-- 			assert_eq(expected[i], p, 2)
-- 		end
-- 	end
--
-- 	test_parents("/", { "/" })
-- 	test_parents("/abc", { "/" })
-- 	test_parents("/abc/def", { "/abc", "/" })
-- 	test_parents("/abc/def/", { "/abc/def", "/abc", "/" })
-- 	test_parents("a/b/c", { "a/b", "a", "." })
--
-- 	test_parents(".", { "." })
-- 	test_parents("abc", { "." })
-- 	test_parents("abc/def", { "abc", "." })
-- 	test_parents("abc/def/", { "abc/def", "abc", "." })
-- end
