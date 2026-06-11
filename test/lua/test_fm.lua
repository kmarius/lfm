---@diagnostic disable: param-type-mismatch

local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(17)

local function should_err(f, desc)
	local success = pcall(f)
	ok(not success, desc)
end

local api = lfm.api
local fm = lfm.fm

test("set_info", function()
	for _, info in ipairs({ "ctime", "atime", "mtime", "size" }) do
		fm.set_info(info)
		ok(fm.get_info() == info, "set_info " .. info)
	end

	should_err(function()
		fm.set_info(nil)
	end, "set_info nil should error")

	should_err(function()
		fm.set_info("abc")
	end, "set_info abc should error")

	should_err(function()
		fm.set_info(3)
	end, "set_info 3 should error")
end)

test("sort", function()
	should_err(function()
		fm.sort(nil)
	end, "sortby nil should error")

	should_err(function()
		fm.sort(7)
	end, "sortby 7 should error")
end)

test("filter", function()
	fm.set_filter("abc")
	ok(fm.get_filter() == "abc", "filter abc")
	fm.set_filter(7)
	ok(fm.get_filter() == "7", "filter 7")
	fm.set_filter()
	ok(fm.get_filter() == nil, "clear filter with nil")
	fm.set_filter("")
	ok(fm.get_filter() == nil, 'clear filter with ""')
end)

test("fuzzy", function()
	fm.set_filter("abc", "fuzzy")
	ok(fm.get_filter() == "abc", "fuzzy abc")
	fm.set_filter(7)
	ok(fm.get_filter() == "7", "fuzzy 7")
	fm.set_filter()
	ok(fm.get_filter() == nil, "clear fuzzy with nil")
	fm.set_filter("")
	ok(fm.get_filter() == nil, 'clear fuzzy with ""')
end)
