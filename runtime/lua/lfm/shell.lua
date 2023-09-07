local log = lfm.log
local sel_or_cur = lfm.sel_or_cur

local M = {}

---Pass files as argv.
M.ARGV = 1

---pass files as an array named `files` (`bash`, `fish` and `tmux` only).
M.ARRAY = 2

local ARGV = M.ARGV
local ARRAY = M.ARRAY

---Wrap a string (ortable of strings) in single quotes.
---@param args string[]|string
---@param sep? string separator (default: " ").
---@return string
function M.escape(args, sep)
	if not args then
		return ""
	end
	if type(args) ~= "table" then
		args = { args }
	end
	sep = sep or " "
	local ret = {}
	for _, a in pairs(args) do
		local s = string.gsub(tostring(a), "'", [['\'']])
		table.insert(ret, "'" .. s .. "'")
	end
	return table.concat(ret, sep)
end

---@class Lfm.Shell.ExecOpts
---@field quiet? boolean show command in the log (default: true)
---@field fork? boolean run the command in the background (default: false)
---@field out? boolean redirect stdout in the ui (default: true)
---@field err? boolean redirect stderr in the ui (default: true)

---Execute a foreground command.
---If `command` is a single string, it is executed as `sh -c command`.
---@param command string|string[]
---@param t? Lfm.Shell.ExecOpts
function M.execute(command, t)
	t = t or {}
	if type(command) == "string" then
		command = { "sh", "-c", command }
	end
	if not t.quiet then
		log.debug(table.concat(command, " "))
	end
	if t.fork then
		lfm.spawn(command, t)
	else
		lfm.execute(command)
	end
end

---Run a command an capture the output.
---@param command string|string[]
---@return string[]
function M.popen(command)
	if type(command) == "table" then
		command = M.escape(command)
	end
	local file = io.popen(command, "r")
	local res = {}
	if file then
		for line in file:lines() do
			table.insert(res, line)
		end
	end
	return res
end

---@class Lfm.Shell.BashOpts
---@field files_via? number
---@field quiet? boolean show command in the log (default: true)
---@field fork? boolean run the command in the background (default: false)
---@field out? boolean redirect stdout in the ui (default: true)
---@field err? boolean redirect stderr in the ui (default: true)

---Build a function from a bash command. Unless `t.files_via == shell.ARGV` the
---functions arguments are passed to the shell.
---@param command string
---@param t? Lfm.Shell.BashOpts
---@return function
function M.bash(command, t)
	t = t or {}
	if t.files_via == ARGV then
		return function()
			M.execute({ "bash", "-c", command, "_", unpack(sel_or_cur()) }, t)
		end
	elseif t.files_via == ARRAY then
		return function(...)
			M.execute({ "bash", "-c", "files=(" .. M.escape(sel_or_cur()) .. "); " .. command, "_", ... }, t)
		end
	else
		return function(...)
			M.execute({ "bash", "-c", command, "_", ... }, t)
		end
	end
end

---@class Lfm.Shell.TmuxOpts
---@field files_via? number
---@field quiet? boolean show command in the log (default: true)
---@field fork? boolean run the command in the background (default: false)
---@field out? boolean redirect stdout in the ui (default: true)
---@field err? boolean redirect stderr in the ui (default: true)

---Build a function from a bash command to run in a `tmux new-window`. Unless
---`t.files_via == shell.ARGV` the functions arguments are passed to the shell.
---@param command string
---@param t? Lfm.Shell.TmuxOpts
---@return function
function M.tmux(command, t)
	t = t or {}
	if t.files_via == ARGV then
		return function()
			M.execute({ "tmux", "new-window", command, unpack(sel_or_cur()) }, t)
		end
	elseif t.files_via == ARRAY then
		return function(...)
			M.execute({
				"tmux",
				"new-window",
				"bash",
				"-c",
				"files=(" .. M.escape(sel_or_cur()) .. "); " .. command,
				"_",
				...,
			}, t)
		end
	else
		return function(...)
			M.execute({ "tmux", "new-window", "bash", "-c", command, "_", ... }, t)
		end
	end
end

---@class Lfm.Shell.FishOpts
---@field files_via? number
---@field tmux? boolean open command in a new tmux window (default: false)
---@field quiet? boolean show command in the log (default: true)
---@field fork? boolean run the command in the background (default: false)
---@field out? boolean redirect stdout in the ui (default: true)
---@field err? boolean redirect stderr in the ui (default: true)

---Build a function from a shell command. Unless `t.files_via == shell.ARGV` the
---functions arguments are passed to the shell.
---@param command string
---@param t? Lfm.Shell.FishOpts
---@return function
function M.fish(command, t)
	t = t or {}
	if t.files_via == ARGV then
		return function()
			M.execute({ "fish", "-c", command, unpack(sel_or_cur()) }, t)
		end
	elseif t.files_via == ARRAY then
		if t.tmux then
			return function(...)
				M.execute({
					"tmux",
					"new-window",
					"fish",
					"-c",
					"set -U files " .. M.escape(sel_or_cur()),
					"-c",
					command,
					"--",
					...,
				}, t)
			end
		else
			return function(...)
				M.execute({ "fish", "-c", "set -U files " .. M.escape(sel_or_cur()), "-c", command, "--", ... }, t)
			end
		end
	else
		return function(...)
			M.execute({ "fish", "-c", command, "--", ... }, t)
		end
	end
end

---@class Lfm.Shell.ShOpts
---@field files_via? number
---@field quiet? boolean show command in the log (default: true)
---@field fork? boolean run the command in the background (default: false)
---@field out? boolean redirect stdout in the ui (default: true)
---@field err? boolean redirect stderr in the ui (default: true)

---Build a function from a shell command. Unless `t.files_via == shell.ARGV` the
---functions arguments are passed to the shell.
---@param command string
---@param t? Lfm.Shell.ShOpts
---@return function
function M.sh(command, t)
	t = t or {}
	if t.files_via == ARGV then
		return function()
			M.execute({ "sh", "-c", command, "_", sel_or_cur() }, t)
		end
	elseif t.files_via == ARRAY then
		lfm.error("sh does not support arrays")
		return function() end
	else
		return function(...)
			M.execute({ "sh", "-c", command, ... }, t)
		end
	end
end

return M
