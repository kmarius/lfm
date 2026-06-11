---@diagnostic disable: param-type-mismatch

local tap = require("tap")
local ok = tap.ok
local test = tap.test

tap.init(13)

local function should_err(f, desc)
	local success = pcall(f)
	ok(not success, desc)
end

local o = lfm.o

test("ratios", function()
	local ratios = o.ratios

	o.ratios = { 1 }
	ok(#o.ratios == 1 and o.ratios[1] == 1, "ratios: single pane")

	o.ratios = { 1, 2 }
	ok(#o.ratios == 2 and o.ratios[1] == 1 and o.ratios[2] == 2, "ratios: dual pane")

	o.ratios = { 1, 2, 3, 4 }
	ok(#o.ratios == 4, "ratios: quad pane")

	should_err(function()
		o.ratios = {}
	end, "empty ratios should error")

	should_err(function()
		o.ratios = { 1, 0 }
	end, "ratios with zero should error")

	should_err(function()
		o.ratios = { 1, -1 }
	end, "ratios with negative number should error")

	o.ratios = ratios
end)

test("preview", function()
	o.preview = false
	ok(o.preview == false, "can unset preview")
	o.preview = true
	ok(o.preview == true, "can set preview")
end)

test("truncatechar", function()
	o.truncatechar = "…"
	ok(o.truncatechar == "…", "can set truncatechar")
	should_err(function()
		o.truncatechar = nil
	end, "truncatechar can't be nil")
	-- should_err(function()
	-- 	o.truncatechar = string.char(0xed, 0xa0, 0x80)
	-- end, "truncatechar can't be must be valid character")
end)

test("linkchars", function()
	o.linkchars = "abc"
	ok(o.linkchars == "abc", "can set linkchars")
	should_err(function()
		o.linkchars = nil
	end, "linkchars can't be nil")
	should_err(function()
		o.linkchars = "01234567890abcdefgh"
	end, "linkchars can't be too long")
end)
