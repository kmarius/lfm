local M = {}

--- Output to log
local logfile = io.open(assert(os.getenv("TAP_RESULT")), "w")
if not logfile then
	lfm.quit(1)
end
logfile:setvbuf("line")

lfm.api.add_hook("on_exit", function()
	logfile:close()
end)

function M.log(...)
	logfile:write(...)
	logfile:write("\n")
end

-- Return status for lfm is set to 1 if any of the tests fail
M.ret = 0

---Initialize TAP
---@param n number The number of test cases
function M.init(n)
	M.log("1..", n)
end

local counter = 1
function M.ok(assert_true, desc, ...)
	local msg = (assert_true and "ok " or "not ok ") .. counter
	if not assert_true then
		M.ret = 1
	end
	if desc then
		msg = msg .. " - " .. string.format(desc, ...)
	end
	M.log(msg)
	counter = counter + 1
end

function M.test(name, f)
	print(name)
	f()
end

return M
