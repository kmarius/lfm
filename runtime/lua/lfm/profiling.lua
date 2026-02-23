-- Print startup profile to log after start
local M = {}

local lfm = lfm

local ffi = require("ffi")
local C = ffi.C

ffi.cdef([[
struct profiling_entry {
  uint64_t ts, diff;
  const char *name;
  int depth;
  bool is_complete;
};

struct profiling_data {
  uint64_t startup;
  int num_entries;
  struct profiling_entry entries[];
};

struct profiling_data *get_profiling_data();
]])

local data = C.get_profiling_data()

function M.get_lines()
	local entries = {}

	-- every incomplete entry artificially increases the depth of all following entries
	local incomplete = 0

	local longest = 0
	for i = 0, data.num_entries - 1 do
		local entry = data.entries[i]
		if entry.is_complete then
			local name = ffi.string(entry.name)
			local depth = entry.depth - incomplete
			local length = #name + 2 * depth
			if length > longest then
				longest = length
			end
			table.insert(entries, {
				name = name,
				diff = tonumber(entry.diff),
				depth = depth,
				length = length,
			})
		else
			incomplete = incomplete + 1
		end
	end

	entries.header = "profiled data in microseconds"
	for i, entry in ipairs(entries) do
		local padding_before = string.rep("  ", entry.depth)
		local padding = string.rep(" ", longest - entry.length)
		entries[i] = string.format("%s%s%s % 5d", padding_before, entry.name, padding, entry.diff)
	end

	return entries
end

function M.log_lines()
	local lines = M.get_lines()
	lfm.log.info(lines.header)
	for _, line in ipairs(lines) do
		lfm.log.info(line)
	end
end

function M.startuptime()
	local lines = M.get_lines()
	table.insert(lines, 1, lines.header)
	lfm.execute({ "less" }, { stdin = lines })
end

return M
