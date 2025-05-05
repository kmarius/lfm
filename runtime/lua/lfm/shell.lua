local M = { _NAME = ... }

local lfm = lfm

local sel_or_cur = lfm.api.fm_sel_or_cur
local lfm_execute = lfm.execute
local lfm_spawn = lfm.spawn
local table_insert = table.insert

---@enum Lapi.fm_Shell.FilesVia
local FilesVia = {
	ARGV = 0,
	ARRAY = 1,
}

---Pass files via argv.
M.ARGV = FilesVia.ARGV

---Pass files as an array named `files` if supported by the shell.
M.ARRAY = FilesVia.ARRAY

local ARGV = FilesVia.ARGV
local ARRAY = FilesVia.ARRAY

---Wrap a string (or table of strings) in single quotes and join them with an optional separator.
---```lua
---    local escaped = escape({"file with witespace.txt", "audio.mp3"}, " ")
---```
---@param args string[]|string
---@param sep? string separator (default: " ").
---@return string
---@nodiscard
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
		table_insert(ret, "'" .. s .. "'")
	end
	return table.concat(ret, sep)
end
local escape = M.escape

---Run a command an capture the output.
---```lua
---    local tmpdir = lfm.shell.popen("mktemp -d")[1]
---```
---@param command string|string[]
---@return string[]
function M.popen(command)
	if type(command) == "table" then
		command = escape(command)
	end
	local file = io.popen(command, "r")
	local res = {}
	if file then
		for line in file:lines() do
			table_insert(res, line)
		end
	end
	return res
end

do
	---@class Lapi.fm_Shell.Sh.SpawnOpts : Lapi.fm_SpawnOpts
	---@field files_via? Lapi.fm_Shell.FilesVia

	---@class Lapi.fm_Shell.Sh.ExecOpts
	---@field files_via? Lapi.fm_Shell.FilesVia

	---@class Lapi.fm_Shell.Sh.BuildOpts: Lapi.fm_Shell.Sh.ExecOpts, Lapi.fm_Shell.Sh.SpawnOpts
	---@field fg? true

	---Spawn a shell process in the background.
	---```lua
	---    lfm.shell.sh.spawn("mv *.txt somedir")
	---
	---    lfm.shell.sh.spawn('mv "$@" somedir', { files_via = lfm.shell.ARGV })
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Sh.SpawnOpts
	---@param ... string Extra arguments will be passed to the command (unless opts.files_via == ARGV)
	local function spawn(command, opts, ...)
		opts = opts or {}
		local cmd = { "sh", "-c", command }
		if opts.files_via == ARGV then
			cmd[#cmd + 1] = "_"
			for _, file in ipairs(sel_or_cur()) do
				cmd[#cmd + 1] = file
			end
		elseif opts.files_via == ARRAY then
			error("files_via ARRAY not supported for sh")
		else
			local n = select("#", ...)
			if n > 0 then
				cmd[#cmd + 1] = "_"
				local args = { ... }
				for i = 1, n do
					cmd[#cmd + 1] = args[i]
				end
			end
		end
		lfm_spawn(cmd, opts)
	end

	---Spawn a shell process in the foreground.
	---```lua
	---    lfm.shell.sh.execute("nvim *.txt")
	---
	---    lfm.shell.sh.execute('nvim "$@"', { files_via = lfm.shell.ARGV })
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Sh.ExecOpts
	---@param ... string Extra arguments will be passed to the command (unless opts.files_via == ARGV)
	local function execute(command, opts, ...)
		opts = opts or {}
		local cmd = { "sh", "-c", command }
		if opts.files_via == ARGV then
			cmd[#cmd + 1] = "_"
			for _, file in ipairs(sel_or_cur()) do
				cmd[#cmd + 1] = file
			end
		elseif opts.files_via == ARRAY then
			error("files_via ARRAY not supported for sh")
		else
			local n = select("#", ...)
			if n > 0 then
				cmd[#cmd + 1] = "_"
				local args = { ... }
				for i = 1, n do
					cmd[#cmd + 1] = args[i]
				end
			end
		end
		lfm_execute(cmd)
	end

	---Build a function that executes a shell command.
	---```lua
	---    local f = lfm.shell.sh.build("echo hey", { fork = true })
	---    f()
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Sh.BuildOpts
	---@return function
	local function build(command, opts)
		opts = opts or {}
		if opts.fg then
			return function(...)
				execute(command, opts --[[@as Lapi.fm_Shell.Sh.BuildOpts]], ...)
			end
		else
			return function(...)
				spawn(command, opts --[[@as Lapi.fm_Shell.Sh.BuildOpts]], ...)
			end
		end
	end

	M.sh = { spawn = spawn, execute = execute, build = build }
end

do
	---@class Lapi.fm_Shell.Bash.SpawnOpts : Lapi.fm_SpawnOpts
	---@field files_via? Lapi.fm_Shell.FilesVia

	---@class Lapi.fm_Shell.Bash.ExecOpts
	---@field files_via? Lapi.fm_Shell.FilesVia

	---@class Lapi.fm_Shell.Bash.BuildOpts : Lapi.fm_Shell.Bash.ExecOpts, Lapi.fm_Shell.Bash.SpawnOpts
	---@field fg? true

	---Spawn a shell process in the background.
	---```lua
	---    lfm.shell.bash.spawn("mv *.txt somedir")
	---
	---    lfm.shell.bash.spawn('mv "$@" somedir', { files_via = lfm.shell.ARGV })
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Bash.SpawnOpts
	---@param ... string Extra arguments will be passed to the command (unless opts.files_via == ARGV)
	local function spawn(command, opts, ...)
		opts = opts or {}
		local cmd = { "bash", "-c", command }
		if opts.files_via == ARGV then
			cmd[#cmd + 1] = "_"
			for _, file in ipairs(sel_or_cur()) do
				cmd[#cmd + 1] = file
			end
		else
			if opts.files_via == ARRAY then
				cmd[3] = string.format("files=(%s); %s", escape(sel_or_cur()), command)
			end
			local n = select("#", ...)
			if n > 0 then
				cmd[#cmd + 1] = "_"
				local args = { ... }
				for i = 1, n do
					cmd[#cmd + 1] = args[i]
				end
			end
		end
		lfm_spawn(cmd, opts)
	end

	---Spawn a shell process in the foreground.
	---```lua
	---    lfm.shell.bash.execute("nvim *.txt")
	---
	---    lfm.shell.bash.execute('nvim "$@"', { files_via = lfm.shell.ARGV })
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Bash.ExecOpts
	---@param ... string Extra arguments will be passed to the command (unless opts.files_via == ARGV)
	local function execute(command, opts, ...)
		opts = opts or {}
		local cmd = { "bash", "-c", command }
		if opts.files_via == ARGV then
			cmd[#cmd + 1] = "_"
			for _, file in ipairs(sel_or_cur()) do
				cmd[#cmd + 1] = file
			end
		else
			if opts.files_via == ARRAY then
				cmd[3] = string.format("files=(%s); %s", escape(sel_or_cur()), command)
			end
			local n = select("#", ...)
			if n > 0 then
				cmd[#cmd + 1] = "_"
				local args = { ... }
				for i = 1, n do
					cmd[#cmd + 1] = args[i]
				end
			end
		end
		lfm_execute(cmd)
	end

	---Build a function that executes a shell command.
	---```lua
	---    local f = lfm.shell.bash.build("echo hey", { fork = true })
	---    f()
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Bash.BuildOpts
	---@return function
	local function build(command, opts)
		opts = opts or {}
		if opts.fg then
			return function(...)
				execute(command, opts, ...)
			end
		else
			return function(...)
				spawn(command, opts, ...)
			end
		end
	end
	M.bash = { spawn = spawn, execute = execute, build = build }
end

do
	---@class Lapi.fm_Shell.Fish.SpawnOpts : Lapi.fm_SpawnOpts
	---@field files_via? Lapi.fm_Shell.FilesVia

	---@class Lapi.fm_Shell.Fish.ExecOpts
	---@field files_via? Lapi.fm_Shell.FilesVia

	---@class Lapi.fm_Shell.Fish.BuildOpts : Lapi.fm_Shell.Fish.ExecOpts ,Lapi.fm_Shell.Fish.SpawnOpts
	---@field fg? true

	---Spawn a shell process in the background.
	---```lua
	---    lfm.shell.fish.spawn("mv *.txt somedir")
	---
	---    lfm.shell.fish.spawn('mv "$@" somedir', { files_via = lfm.shell.ARGV })
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Fish.SpawnOpts
	---@param ... string Extra arguments will be passed to the command (unless opts.files_via == ARGV)
	local function spawn(command, opts, ...)
		opts = opts or {}
		local cmd = { "fish", "-c", command }
		if opts.files_via == ARGV then
			cmd[#cmd + 1] = "_"
			for _, file in ipairs(sel_or_cur()) do
				cmd[#cmd + 1] = file
			end
		else
			if opts.files_via == ARRAY then
				table_insert(cmd, 2, "-c")
				table_insert(cmd, 2, "set files " .. escape(sel_or_cur()))
			end
			local n = select("#", ...)
			if n > 0 then
				cmd[#cmd + 1] = "--"
				local args = { ... }
				for i = 1, n do
					cmd[#cmd + 1] = args[i]
				end
			end
		end
		lfm_spawn(cmd, opts)
	end

	---Spawn a shell process in the foreground.
	---```lua
	---    lfm.shell.fish.execute("nvim *.txt")
	---
	---    lfm.shell.fish.execute('nvim "$@"', { files_via = lfm.shell.ARGV })
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Fish.ExecOpts
	---@param ... string Extra arguments will be passed to the command (unless opts.files_via == ARGV)
	local function execute(command, opts, ...)
		opts = opts or {}
		local cmd = { "fish", "-c", command }
		if opts.files_via == ARGV then
			cmd[#cmd + 1] = "--"
			for _, file in ipairs(sel_or_cur()) do
				cmd[#cmd + 1] = file
			end
		else
			if opts.files_via == ARRAY then
				table_insert(cmd, 2, "-c")
				table_insert(cmd, 2, "set files " .. escape(sel_or_cur()))
			end
			local n = select("#", ...)
			if n > 0 then
				cmd[#cmd + 1] = "--"
				local args = { ... }
				for i = 1, n do
					cmd[#cmd + 1] = args[i]
				end
			end
		end
		lfm_execute(cmd)
	end

	---Build a function that executes a shell command.
	---```lua
	---    local f = lfm.shell.fish.build("echo hey", { fork = true })
	---    f()
	---```
	---@param command string
	---@param opts? Lapi.fm_Shell.Fish.BuildOpts
	---@return function
	local function build(command, opts)
		opts = opts or {}
		if opts.fg then
			return function(...)
				execute(command, opts --[[@as Lapi.fm_Shell.Fish.BuildOpts]], ...)
			end
		else
			return function(...)
				spawn(command, opts --[[@as Lapi.fm_Shell.Fish.BuildOpts]], ...)
			end
		end
	end
	M.fish = { spawn = spawn, execute = execute, build = build }
end

return M
