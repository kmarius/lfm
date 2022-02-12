local log = lfm.log
local sel_or_cur = lfm.sel_or_cur

local M = {}

M.ARGV = 1
M.ARRAY = 2
M.APPPEND = 3 -- TODO: implement (on 2021-07-29)

local ARGV = M.ARGV
local ARRAY = M.ARRAY

---Wrap a table of strings in single quotes and concatenate.
---@param args table Strings to concatenate.
---@param sep? string Concatenation separator (default: " ").
---@return string
function M.escape(args, sep)
	if not args then return "" end
	if type(args) ~= "table" then args = {args} end
	sep = sep or " "
	local ret = {}
	for _, a in pairs(args) do
		local s = string.gsub(tostring(a), "'", [['\'']])
		table.insert(ret, "'" .. s .. "'")
	end
	return table.concat(ret, sep)
end

function M.execute(command, t)
	t = t or {}
	if type(command) == "string" then
		command = {"sh", "-c", command}
	end
	if not t.quiet then
		log.debug(table.concat(command, " "))
	end
	lfm.execute(command, t)
end

-- Run a command an capture the output
function M.popen(command)
	if type(command) == "table" then
		command = M.escape(command)
	end
	local file = io.popen(command, "r")
	local res = {}
	for line in file:lines() do
		table.insert(res, line)
	end
	return res
end

function M.bash(command, t)
	t = t or {}
	if t.files == ARGV then
		return function()
			M.execute({"bash", "-c", command, "_", unpack(sel_or_cur())}, t)
		end
	elseif t.files == ARRAY then
		return function(...)
			M.execute({"bash", "-c", "files=("..M.escape(sel_or_cur()).."); "..command, "_", ...}, t)
		end
	else
		return function(...)
			M.execute({"bash", "-c", command, "_", ...}, t)
		end
	end
end

-- bash only for now
function M.tmux(command, t)
	t = t or {}
	if t.files == ARGV then
		return function()
			M.execute({"tmux", "new-window", command, unpack(sel_or_cur())}, t)
		end
	elseif t.files == ARRAY then
		return function(...)
			M.execute({"tmux", "new-window", "bash", "-c", "files=("..M.escape(sel_or_cur()).."); "..command, "_", ...}, t)
		end
	else
		return function(...)
			M.execute({"tmux", "new-window", "bash", "-c", command, "_", ...}, t)
		end
	end
end

-- Fish does not actually need "--" after the command to pass argv
function M.fish(command, t)
	t = t or {}
	if t.files == ARGV then
		return function()
			M.execute({"fish", "-c", command, sel_or_cur()}, t)
		end
	elseif t.files == ARRAY then
		if t.tmux then
			return function(...)
				M.execute({"tmux", "new-window", "fish", "-c", "set -U files "..M.escape(sel_or_cur()), "-c", command, "--", ...}, t)
			end
		else
			return function(...)
				M.execute({"fish", "-c", "set -U files "..M.escape(sel_or_cur()), "-c", command, "--", ...}, t)
			end
		end
	else
		return function(...)
			M.execute({"fish", "-c", command, "--", ...}, t)
		end
	end
end

function M.sh(command, t)
	t = t or {}
	if t.files == ARGV then
		return function()
			M.execute({"sh", "-c", command, "_", sel_or_cur()}, t)
		end
	elseif t.files == ARRAY then
		lfm.error("files_as_array not supported for sh")
		return function() end
	else
		return function(...)
			M.execute({"sh", "-c", command, ...}, t)
		end
	end
end

return M
