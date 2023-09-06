local lfm = lfm

local config = lfm.config

local M = {}

M.palette = {
	black = "0",
	red = "1",
	green = "2",
	yellow = "3",
	blue = "4",
	magenta = "5",
	cyan = "6",
	white = "7",

	bright_black = "8",
	bright_red = "9",
	bright_green = "10",
	bright_yellow = "11",
	bright_blue = "12",
	bright_magenta = "13",
	bright_cyan = "14",
	bright_white = "15",
}

---Read colors from the `LS\_COLORS` environment variable.
---@return table colors The table of color mappings that can be passed to lfm.config.colors
function M.load_lscolors()
	local patterns = {}
	for str in string.gmatch(os.getenv("LS_COLORS"), "[^:]+") do
		local ext, colors = string.match(str, "^%*(%.[^=]*)=(.*)")
		if ext then
			local t = {}
			for num in string.gmatch(colors, "[^;]+") do
				local i = tonumber(num)
				if i < 9 then
					-- TODO:  (on 2021-08-21)
					-- 00	Default colour
					-- 01	Bold
					-- 04	Underlined
					-- 05	Flashing text
					-- 07	Reversed
					-- 08	Concealed
				elseif i < 38 then
					t.fg = tostring(i - 30)
				elseif i < 48 then
					t.bg = tostring(i - 30)
				elseif i < 98 then
					t.fg = tostring(i - 90 + 8)
				elseif i < 108 then
					t.bg = tostring(i - 100 + 8)
				end
			end
			if ext and next(t) then
				table.insert(patterns, { color = t, ext = { ext } })
			end
		else
			local type, colors = string.match(str, "^(%.[^=]*)=(.*)")
			-- TODO: set stuff (on 2021-08-21)
		end
	end
	return patterns
end

---Computes the single integer corresponding to the color defined by (r,g,b).
---@param r number 0 <= r <= 255
---@param b number 0 <= b <= 255
---@param g number 0 <= g <= 255
---@return number
function M.rgb(r, g, b)
	return (r * 256 + g) * 256 + b
end

---@param t table
function M.add(t)
	t = t or {}
	for k, v in pairs(t) do
		config.colors[k] = v
	end
end

---@param t table
function M.set(t)
	-- lfm.colors_clear()
	t = t or {}
	for k, v in pairs(t) do
		config.colors[k] = v
	end
end

return M
