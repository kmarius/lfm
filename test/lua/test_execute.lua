local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(20)

test("exec_status", function()
	local ret

	ret = lfm.execute({ "true" })
	ok(ret.status == 0, "exit status 0")

	ret = lfm.execute({ "sh", "-c", "exit 7" })
	ok(ret.status == 7, "exit status 7")
end)

test("exec_capture_stdout", function()
	local ret = lfm.execute({ "echo", "TESTVAL" }, {
		capture_stdout = true,
	})
	ok(ret.stdout[1] == "TESTVAL", "stdout captured")
end)

test("exec_capture_stdout_long", function()
	-- more input than fits the two pipes around cat
	local input = string.rep("_", math.pow(2, 18))
	local ret = lfm.execute({ "cat" }, {
		stdin = input,
		capture_stdout = true,
	})
	ok(#ret.stdout == 1, "stdout is one line")
	ok(#ret.stdout[1] == #input, "stdout is long enough %d", #ret.stdout[1])
	ok(ret.stdout[1] == input, "stdout captured correctly")
end)

test("exec_capture_stdout_table", function()
	local input = string.rep("_", math.pow(2, 18))
	local ret = lfm.execute({ "cat" }, {
		stdin = { input },
		capture_stdout = true,
	})
	ok(#ret.stdout == 1, "stdout is one line")
	ok(#ret.stdout[1] == #input, "stdout is long enough %d", #ret.stdout[1])
	ok(ret.stdout[1] == input, "stdout captured correctly")
end)

test("exec_capture_stdout_long_table", function()
	local input = string.rep("_", math.pow(2, 18))
	local ret = lfm.execute({ "cat" }, {
		stdin = { input },
		capture_stdout = true,
	})
	ok(#ret.stdout == 1, "stdout is one line")
	ok(#ret.stdout[1] == #input, "stdout is long enough %d", #ret.stdout[1])
	ok(ret.stdout[1] == input, "stdout captured correctly")
end)

test("exec_capture_stdout_with_nul", function()
	local ret = lfm.execute({ "printf", "abc\\0def" }, {
		capture_stdout = true,
	})
	ok(ret.stdout[1] == "abc\0def")
end)

test("exec_capture_stderr", function()
	local ret = lfm.execute({ "sh", "-c", "echo TESTVAL >&2" }, {
		capture_stderr = true,
	})
	ok(ret.stderr[1] == "TESTVAL", "stderr captured")
end)

test("exec_capture_stderr_with_nul", function()
	local ret = lfm.execute({ "sh", "-c", "echo 'abc\\0def' >&2" }, {
		capture_stderr = true,
	})
	ok(ret.stderr[1] == "abc\0def", "stderr captured")
end)

test("exec_send_stdin", function()
	local ret = lfm.execute({ "cat" }, {
		capture_stdout = true,
		stdin = { "TESTVAL1", "TESTVAL2" },
	})
	ok(#ret.stdout == 2, "stdin sent")
	ok(ret.stdout[1] == "TESTVAL1", "stdin sent first line")
	ok(ret.stdout[2] == "TESTVAL2", "stdin sent second line")
end)

test("exec_send_stdin_with_nul", function()
	local ret = lfm.execute({ "cat" }, {
		stdin = { "abc\0def" },
		capture_stdout = true,
	})
	ok(ret.stdout[1] == "abc\0def", "stdin with nul received")
end)

test("exec_env", function()
	local ret = lfm.execute({ "sh", "-c", 'test "$TESTVAR" = TESTVAL' }, {
		env = { TESTVAR = "TESTVAL" },
	})
	ok(ret.status == 0, "environment variable passed")
end)
