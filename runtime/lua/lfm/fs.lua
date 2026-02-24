-- https://neovim.io/doc/user/lua.html#vim.fs

local lfm = lfm
local fn = lfm.fn

local dirent = require("posix.dirent")
local stat = require("posix.sys.stat")
local stdlib = require("posix.stdlib")

local M = {}

---
---Check whether a file exists.
---
---```lua
---  if fs.exists("path/to/some/file.txt") then
---    print("file exists")
---  end
---```
---
---@param path string
---@return boolean
function M.exists(path)
	lfm.validate("path", path, "string")
	return stat.lstat(path) ~= nil
end

---
---Get the absolute path of the argument. Replaces ~ and prepends PWD if necessary.
---
---```lua
---  local abs = fs.abspath("path/to/some/file.txt")
---  local abs2 = fs.abspath("~/Desktop")
---```
---
---@param path string
---@return string
function M.abspath(path)
	lfm.validate("path", path, "string")
	if path:sub(1, 1) == "/" then
		return path
	end
	if path == "~" then
		return os.getenv("HOME") or ""
	end
	if path:sub(1, 2) == "~/" then
		return os.getenv("HOME") .. path:sub(2)
	end
	return fn.getpwd() .. "/" .. path
end

---
---Get the basename of a path.
---
---```lua
---  local base = fs.basename("path/to/some/file.txt") -- "file.txt"
---```
---
---@param path? string
---@return string
---@overload fun():nil
function M.basename(path)
	lfm.validate("path", path, "string", true)
	if not path or not path:find("/") then
		return path
	end
	return (path:gsub("^.*/", ""))
end

---@class Lfm.Fs.DirOpts
---@field depth? integer Default: `1`
---@field skip? fun(dir_name: string):boolean If function returns `false`, don't traverse into the subdirectory
---@field follow? boolean Follow symlinks. Default: `false`

---@alias Lfm.Fs.Type
---| '"file"'
---| '"directory"'
---| '"link"'
---| '"fifo"'
---| '"socket"'
---| '"char"'
---| '"block"'
---| '"unknown"'

local types = {
	[stat.S_IFREG] = "file",
	[stat.S_IFDIR] = "directory",
	[stat.S_IFLNK] = "link",
	[stat.S_IFIFO] = "fifo",
	[stat.S_IFSOCK] = "socket",
	[stat.S_IFCHR] = "char",
	[stat.S_IFBLK] = "block",
}

---
---Traverse directory tree in level order.
---
---```lua
---  -- traverse only the current directory
---  for name, ftype in fs.dir(".") do
---    print(name, ftype)
---  end
---```
---
---```lua
---  -- traverse 2 levels deep and follow symlinks
---  for name, ftype in fs.dir(".", { depth = 2, follow = true }) do
---    print(name, ftype)
---  end
---```
---
---```lua
---  -- traverse 3 levels deep, but not into "build" directory
---  local function skip_build(name)
---    return name ~= "build"
---  end
---  for name, ftype in fs.dir(".", { depth = 3, skip = skip_build }) do
---    print(name, ftype)
---  end
---```
---
---@param path string
---@param opts? Lfm.Fs.DirOpts
---@return fun():(string, Lfm.Fs.Type)
function M.dir(path, opts)
	lfm.validate("path", path, "string")
	lfm.validate("opts", opts, "table", true)
	opts = opts or {}
	lfm.validate("opts.depth", opts.depth, "number", true)
	lfm.validate("opts.depth", opts.depth, function(v)
		return v >= 1
	end, true, "positive number")
	lfm.validate("opts.skip", opts.skip, "function", true)
	lfm.validate("opts.follow", opts.follow, "boolean", true)

	return coroutine.wrap(function()
		local start = M.normalize(path)
		local depth = opts.depth or 1
		local prefix_len = #start + 1
		local dirs = { start }
		local skip = opts.skip
		local follow = opts.follow
		while depth > 0 and #dirs > 0 do
			local next_dirs = {}
			for _, dir in ipairs(dirs) do
				for file in dirent.files(dir) do
					if file ~= "." and file ~= ".." then
						file = M.joinpath(dir, file)
						local st = stat.lstat(file) or { st_mode = -1 }
						local mode = bit.band(st.st_mode, stat.S_IFMT)
						local type = types[mode] or "unknown"
						-- return with prefix stripped
						local res = string.sub(file, prefix_len + 1)
						coroutine.yield(res, type)
						if follow and type == "link" then
							-- check if we need to follow the link
							st = stat.stat(file) or { st_mode = -1 }
							mode = bit.band(st.st_mode, stat.S_IFMT)
							type = types[mode] or "unknown"
						end
						if depth > 1 and type == "directory" and (not skip or skip(res)) then
							table.insert(next_dirs, file)
						end
					end
				end
			end
			dirs = next_dirs
			depth = depth - 1
		end
	end)
end

---
---Get the directory component of a path.
---
---```lua
---  local dir = fs.dirname("path/to/some/file.txt") -- "path/to/some"
---```
---
---@param path string
---@return string
---@overload fun():nil
function M.dirname(path)
	lfm.validate("path", path, "string", true)

	if not path then
		return path
	end
	if not path:find("/") then
		return "."
	end
	path = path:gsub("/[^/]*$", "")
	if path == "" then
		return "/"
	end
	return path
end

---@class Lfm.Fs.FindOpts
---@field path? string Starting path for the search (default: `"."`)
---@field upward? boolean Search upwards in all parents of `path` (default: `false`)
---@field stop? string Stop searching upwards once this directory is reached (default: `nil`)
---@field type? Lfm.Fs.Type Limit search to a specific file type (default: `nil`)
---@field limit? number Limit number of results (default: `1`)
---@field follow? boolean Follow symlinks (default: `false`)

---
---Find files.
---
---```lua
---  -- find the first occurrance of file.txt in the current directory tree
---  local files = fs.find("file.txt") -- { "/path/to/PWD/some/subpath/file.txt" }
---
---  -- find up to 5 occurrance of file.txt or image.png in the /some/path tree
---  local files = fs.find({ "file.txt", "image.png" }, { path = "/some/path" })
---
---  -- find the first occurrance of CMakeLists.txt in the parent and its parent directories
---  local files = fs.find("CMakeLists.txt", { upward = true, stop = "/path/to/project" })
---
---  -- find all fifos in /run
---  local files = fs.find(function(name, path) return true end, { path = "/run", type = "fifo" })
---```
---
---@param names (string|string[]|fun(name: string, path: string): boolean)
---@param opts? Lfm.Fs.FindOpts
---@return string[]
function M.find(names, opts)
	lfm.validate("names", names, { "string", "table", "function" })
	lfm.validate("opts", opts, "table", true)
	opts = opts or {}
	lfm.validate("opts.path", opts.path, "string", true)
	lfm.validate("opts.upward", opts.upward, "boolean", true)
	lfm.validate("opts.stop", opts.stop, "string", true)
	lfm.validate("opts.type", opts.type, "string", true)
	lfm.validate("opts.limit", opts.limit, "number", true)
	lfm.validate("opts.limit", opts.limit, function(val)
		return val > 0
	end, true, "a positive number")
	lfm.validate("opts.follow", opts.follow, "boolean", true)

	local path = M.abspath(opts.path or ".")
	local limit = opts.limit or 1
	local wanted_type = opts.type

	local files = {}

	local name_matches = names
	if type(names) == "string" then
		name_matches = function(name, _)
			return name == names
		end
	elseif type(names) == "table" then
		local set = {}
		for _, name in ipairs(names) do
			set[name] = true
		end
		name_matches = function(name, _)
			return set[name]
		end
	end

	if opts.upward then
		for dir in M.parents((path:gsub("/*$", "/"))) do
			if opts.stop and dir == opts.stop then
				return files
			end
			for file, type in M.dir(dir, { follow = opts.follow }) do
				if not wanted_type or type == wanted_type then
					file = M.normalize(M.joinpath(dir, file), { expand_env = false })
					if name_matches(M.basename(file), file) then
						table.insert(files, file)
						if #files >= limit then
							return files
						end
					end
				end
			end
		end
	else
		-- I forgot, why depth 10? Should it be an option?
		for file, type in M.dir(path, { follow = opts.follow, depth = 10 }) do
			if not wanted_type or type == wanted_type then
				file = M.normalize(M.joinpath(path, file), { expand_env = false })
				if name_matches(M.basename(file), file) then
					table.insert(files, file)
					if #files >= limit then
						return files
					end
				end
			end
		end
	end
	return files
end

---
---Join path components.
---
---```lua
---  local path = fs.joinpath("/path/to", "some", "file.txt") -- "/path/to/some/file.txt"
---```
---
---@param ... string
---@return string
function M.joinpath(...)
	local path = table.concat({ ... }, "/")
	path = path:gsub("/+", "/")
	return path
end

---@class Lfm.Fs.NormalizeOpts
---@field expand_env? boolean Expand environment variables (default: `false`)

---
---Normalize a path by replacing all .., ./, ~ and returning an absolute path. Optionally replacing environment variables.
---
---```lua
---  local path = fs.normalize("/path/to/some/dir/../file.txt") -- "/path/to/some/file.txt"
---
---  -- expand environment variables, only of the form $VAR, not ${VAR}
---  local path = fs.normalize("$HOME/path/to/some/file.txt", { expand_env = true })
---```
---
---@param path string
---@param opts? Lfm.Fs.NormalizeOpts
---@return string
function M.normalize(path, opts)
	lfm.validate("path", path, "string")
	lfm.validate("opts", opts, "table", true)
	opts = opts or {}
	lfm.validate("opts.expand_env", opts.expand_env, "boolean", true)

	-- make sure we have a / at the end - we will remove it later
	path = path:gsub("/*$", "/")

	-- replace leading ~
	if path:sub(1, 2) == "~/" then
		path = os.getenv("HOME") .. path:sub(2)
	end

	-- expand environment variables
	if opts.expand_env == nil or opts.expand_env then
		path = string.gsub(path, "%$([%u%d_]+)", os.getenv)
	end

	-- eliminate multiple /
	path = path:gsub("/+", "/")

	-- eliminate all /./
	local tmp, n
	repeat
		path, n = path:gsub("/%./", "/")
	until n == 0

	-- remove leading ./
	path = path:gsub("^%./", "")

	-- eliminate somedir/../
	repeat
		tmp = path
		path = path:gsub("([^/]+)/%.%./", function(name)
			if name ~= ".." then
				return ""
			end
		end)
	until tmp == path

	-- previous step can leave us with nothing
	if path == "" then
		return "."
	end

	-- eliminate ^/../
	repeat
		path, n = path:gsub("^/%.%./", "/")
	until n == 0

	-- trailing slash for root stays on
	if path == "/" then
		return "/"
	end

	-- remove trailing /
	assert(path:sub(#path) == "/")
	path = path:sub(1, #path - 1)

	return path
end

---
---Iterate of all parents of a directory.
---
---```lua
---  for parent in fs.parents("/path/to/some/file.txt") do
---    print(parent) -- "/path/to/some", "/path/to", "/path", "/"
---  end
---```
---
---@param start string Absolute path to the starting point.
---@return function
function M.parents(start)
	lfm.validate("start", start, "string")

	return function()
		if not start then
			return nil
		end
		local parent = M.dirname(start)
		if parent == "/" or parent == "." then
			---@diagnostic disable-next-line: cast-local-type
			start = nil
		else
			---@diagnostic disable-next-line: cast-local-type
			start = parent
		end
		return parent
	end
end

---Reserved in nvim for the future
---@class Lfm.Fs.RelpathOpts

---
---Get the path of `target` relative to `base`, if `base` is an ancestor.
---
---```lua
---  local path = fs.relpath("/path/to", "/path/to/some/file.txt") -- "some/file.txt"
---```
---
---@param base string
---@param target string
---@param opts? Lfm.Fs.RelpathOpts
---@return string?
function M.relpath(base, target, opts)
	lfm.validate("base", base, "string")
	lfm.validate("target", target, "string")
	lfm.validate("opts", opts, "table", true)
	opts = opts or {}

	-- add / suffixes to make sure we only match entire components
	if base:sub(#base) ~= "/" then
		base = base .. "/"
	end

	if target:sub(#target) ~= "/" then
		target = target .. "/"
	end

	if target:sub(1, #base) == base then
		if #target == #base then
			return "."
		end
		return target:sub(#base + 1, #target - 1)
	end
	-- base not an ancestor
end

---@class Lfm.Fs.RmOpts
---@field recursive? boolean
---@field force? boolean

---
---Remove a path (spawns `rm`).
---
---```lua
---  fs.rm("/path/to/some/file.txt")
---```
---
---```lua
---  fs.rm("/path/to/some", { recursive = true, force = true })
---```
---
---@param path string
---@param opts? Lfm.Fs.RmOpts
function M.rm(path, opts)
	lfm.validate("path", path, "string")
	lfm.validate("opts", opts, "table", true)
	opts = opts or {}
	lfm.validate("opts.recursive", opts.recursive, "boolean", true)
	lfm.validate("opts.force", opts.force, "boolean", true)

	local cmd = { "rm" }
	if opts.recursive then
		table.insert(cmd, "-r")
	end
	if opts.force then
		table.insert(cmd, "-f")
	end
	table.insert(cmd, "--")
	table.insert(cmd, path)

	lfm.spawn(cmd, { on_stderr = true })
end

---
---Find the root of some subtree using a marker file.
---
---```lua
---  -- finds the parent that contains CMakeLists.txt
---  local root = fs.root(".", "CMakeLists.txt")
---```
---
---```lua
---  -- multiple files
---  local root = fs.root(".", { "CMakeLists.txt", "compile_commands.json" })
---```
---
---```lua
---  -- using a function
---  local root = fs.root(".", function(name)
---    return name:match("^marker.*%.txt$")
---  end)
---```
---
---@param source string Starting point of the search
---@param marker (string|string[]|fun(name: string, path: string): boolean)
---@return string?
function M.root(source, marker)
	lfm.validate("source", source, "string")
	lfm.validate("marker", marker, { "string", "table", "function" })

	local is_marker = marker
	if type(marker) == "string" then
		is_marker = function(file)
			return file == marker
		end
	elseif type(marker) == "table" then
		local markers = {}
		for _, m in ipairs(marker) do
			markers[m] = true
		end
		is_marker = function(file)
			return markers[file]
		end
	end

	-- append "/" so that parents iterator includes the directory itself
	source = M.normalize(source) .. "/"

	for dir in M.parents(source) do
		for file in M.dir(dir) do
			if is_marker(file) then
				return dir
			end
		end
	end
end

---
---Resolve all symlinks in a given path.
---
---```lua
---  local path = fs.realpath("/path/with/symlinks")
---```
---
---@param path string
---@return string
---@return string? err
function M.realpath(path)
	local res, err = stdlib.realpath(path)
	return res, err
end

---
---Split a file name into its prefix and extension.
---
---```lua
---  local stem, ext = fs.split_ext("file.txt") -- "file", ".txt"
---```
---
---@param path string
---@return string stem
---@return string extension
---@overload fun(path: nil): nil
function M.split_ext(path)
	if not path then
		return nil
	end
	local pos = path:find("[^/]%.[^./]*$")
	if not pos then
		return path, ""
	end
	return path:sub(1, pos), path:sub(pos + 1)
end

return M
