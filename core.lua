local home = os.getenv("HOME")
package.path = string.gsub(package.path, "./%?.lua;", "")
if not string.match(package.path, home.."/.config/lfm/lua/") then
	package.path = package.path .. ";"..home..[[/.config/lfm/lua/?.lua]]
end
if not string.match(package.cpath, home.."/.config/lfm/lua/") then
	package.cpath = package.cpath .. ";"..home..[[/.config/lfm/libs/?.so]]
end

local fm = lfm.fm
local log = lfm.log
local ui = lfm.ui
local config = lfm.config

do
	setmetatable(lfm.cmd, {
		__index = function(table, key)
			if key == "prefix" then
				return lfm.cmd.getprefix()
			elseif key == "line" then
				return lfm.cmd.getline()
			end
			return nil
		end,
		__newindex = function(table, key, value)
			if key == "prefix" then
				return lfm.cmd.setprefix(value)
			elseif key == "line" then
				if lfm.cmd.prefix ~= "" then
					lfm.cmd.setline(value)
				end
			end
			return nil
		end,
	})
end

print = function(...)
	local t = {}
	for _, e in pairs({...}) do
		table.insert(t, tostring(e))
	end
	lfm.echo(table.concat(t, " "))
end

function lfm.sel_or_cur()
	local sel = fm.selection
	if not sel[1] then
		sel = {fm.current}
	end
	return sel
end

local hooks = {
	LfmEnter = {},
	ExitPre = {},
	ChdirPre = {},
	ChdirPost = {},
	SelectionChanged = {},
}

function lfm.register_hook(name, f)
	if hooks[name] then
		table.insert(hooks[name], f)
	end
end

function lfm.run_hook(name)
	log.debug("running hook: " .. name)
	if hooks[name] then
		for _, f in pairs(hooks[name]) do
			f()
		end
	end
end

local commands = {}
local cmaps = {}
local nmaps = {}
lfm.modes = {}
modes = lfm.modes

function lfm.register_command(name, f, t)
	t = t or {}
	commands[name] = {f=f, tokenize=t.tokenize == nil and true or t.tokenize}
end

-- function lfm.map(keys, f, t)
-- 	t = t or {}
-- 	nmaps[keys] = {f=f, d=t.desc or ""}
-- end

-- function lfm.cmap(keys, f, t)
-- 	t = t or {}
-- 	cmaps[keys] = {f=f, desc=t.desc or ""}
-- end

function lfm.register_mode(t)
	modes[t.prefix] = t
end

local function cmdenter()
	local line = lfm.cmd.line
	local prefix = lfm.cmd.prefix
	lfm.cmd.clear()
	local mode = modes[prefix]
	-- TODO: allow line to be "" ? (on 2021-07-23)
	if line ~= "" and mode then
		mode.enter(line)
	end
end

local function cmdesc()
	local mode = modes[lfm.cmd.prefix]
	if mode then
		mode.esc()
	end
	lfm.cmd.clear()
end

local function cmddelete()
	lfm.cmd.delete()
	local mode = modes[lfm.cmd.prefix]
	if mode then
		mode.change()
	end
end

local function cmddeleteright()
	lfm.cmd.delete_right()
	local mode = modes[lfm.cmd.prefix]
	if mode then
		mode.change()
	end
end

local function cd(dir)
	fm.chdir(dir or os.getenv("HOME"))
end

-- should return true a file has been opened
local function open()
	local file = fm.open()
	if file then
		lfm.error("no opener configured")
	end
	return false
end

-- do
-- 	local ansi = {
-- 		bold = string.char(27).."[1m",
-- 		normal = string.char(27).."[0m",
-- 	}
-- 	local matches = {}
-- 	local acc = ""
-- 	function lfm.handle_key(key)
-- 		if lfm.cmd.prefix ~= "" then
-- 			local cmap = cmaps[key]
-- 			if cmap then
-- 				cmap.f()
-- 			else
-- 				if key == "<space>" then
-- 					key = " "
-- 				end
-- 				lfm.cmd.insert(key)
-- 				local mode = modes[lfm.cmd.prefix]
-- 				if mode then
-- 					mode.change()
-- 				end
-- 			end
-- 		else
-- 			if key == "<esc>" then
-- 				lfm.cmd.clear()
-- 				if acc == "" then
-- 					local map = nmaps["<esc>"]
-- 					if map then
-- 						map.f()
-- 					end
-- 				else
-- 					matches = {}
-- 					acc = ""
-- 				end
-- 			else
-- 				local tmp
-- 				acc = acc..key
-- 				if next(matches) then
-- 					tmp = matches
-- 					matches = {}
-- 				else
-- 					tmp = nmaps
-- 				end
-- 				local map = tmp[key]
-- 				if map then
-- 					acc = ""
-- 					matches = {}
-- 					ui.menu()
-- 					map.f()
-- 				else
-- 					local menu = {}
-- 					for keys, t in pairs(tmp) do
-- 						if string.find(keys, "^"..key) then
-- 							local tail = string.sub(keys, #key+1, #keys)
-- 							matches[tail] = t
-- 							menu[#menu+1] = acc .. tail .. "\t" .. t.d
-- 						end
-- 					end
-- 					if not next(matches) then
-- 						ui.menu()
-- 						lfm.error("no such bind: " .. acc)
-- 						acc = ""
-- 					else
-- 						if #menu > 0 then
-- 							ui.menu("keys\tcommand", unpack(menu))
-- 						end
-- 					end
-- 				end
-- 			end
-- 		end
-- 	end
-- end

-- TODO: handle <special> keys (on 2021-07-18)
-- handle <
--
do
	local hk = lfm.handle_key
	function lfm.feedkeys(...)
		for _, seq in pairs({...}) do
			hk(seq)
		end
	end
end

commands = {
	cd = {f=cd, tokenize=true},
	quit = {f=lfm.quit, tokenize=true},
}

local function history_prev()
	if lfm.cmd.prefix ~= ":" then
		return
	end
	local line = ui.history_prev()
	if line then
		lfm.cmd.line = line
	end
end
local function history_next()
	if lfm.cmd.prefix ~= ":" then
		return
	end
	local line = ui.history_next()
	if line then
		lfm.cmd.line = line
	end
end

local cmap = lfm.cmap
cmap("<enter>", cmdenter, {desc=""})
cmap("<esc>", cmdesc, {desc=""})
cmap("<backspace>", cmddelete, {desc=""})
cmap("<left>", lfm.cmd.left, {desc=""})
cmap("j", lfm.cmd.left, {desc=""})
cmap("<right>", lfm.cmd.right, {desc=""})
cmap("<up>", history_prev, {desc=""})
cmap("<down>", history_next, {desc=""})
cmap("<home>", lfm.cmd.home, {desc=""})
cmap("<end>", lfm.cmd._end, {desc=""})
cmap("<delete>", cmddeleteright, {desc=""})
-- cmap("<tab>", complete_next)

nmaps = {}

local map = lfm.map
map("f", function() lfm.cmd.prefix = "find: " end, {desc="find"})
map("F", function() lfm.cmd.prefix = "travel: " end, {desc="travel"})
map("zf", function() lfm.cmd.prefix = "filter: " lfm.cmd.line = fm.getfilter() end, {desc="filter"})
map("l", open)
map("q", lfm.quit)
map("j", fm.down)
map("k", fm.up)
map("h", fm.updir)
map("gg", fm.top, {desc="top"})
map("G", fm.bottom, {desc="bottom"})
map("R", function() loadfile("/home/marius/Sync/programming/lfm/core.lua")() end, {desc="reload config"})
map("''", function() fm.mark_load("'") end)
map("zh", function() lfm.config.hidden = not lfm.config.hidden end, {desc="toggle hidden"})
map(":", function() lfm.cmd.prefix = ":" end)
map("/", function() lfm.cmd.prefix = "/" lfm.search("") end)
map("?", function() lfm.cmd.prefix = "?" lfm.search("") end)
map("n", lfm.search_next)
map("N", lfm.search_prev)

-- TODO: make functions to easily enter a mode (on 2021-07-23)
local mode_filter = {
	prefix = "filter: ",
	enter = function(line) fm.filter(line) end,
	esc = function() fm.filter("") end,
	change = function() fm.filter(lfm.cmd.line) end,
}

-- exposed to c
function lfm.exec_expr(line)
	log.debug(line)
	local cmd, args = lfm.tokenize(line)
	if not cmd then
		return
	end
	local command = commands[cmd]
	if command then
		if command.tokenize then
			command.f(unpack(args))
		else
			local arg = string.gsub(line, "^[^ ]*%s*", "")
			command.f(arg)
		end
	else
		log.debug("loadstring: " .. line)
		local bc, err = loadstring(line)
		if bc then
			bc()
		else
			lfm.error("loadstring: "..err)
		end
	end
end

local mode_cmd = {
	prefix = ":",
	enter = function(line) ui.history_append(line) lfm.exec_expr(line) end,
	esc = function() end,
	change = function() end,
}

local mode_search = {
	prefix = "/",
	enter = function(line) lfm.search_next(true) end, -- apply search, keep highlights, move cursor to next match  or stay on current
	esc = function() lfm.search("") end, -- delete everything
	change = function() lfm.search(lfm.cmd.line) end, -- highlight match in UI
}

local mode_search_back = {
	prefix = "?",
	enter = function(line) lfm.search_next(true) end,
	esc = function() lfm.search_back("") end,
	change = function() lfm.search_back(lfm.cmd.line) end,
}

local mode_find = {
	prefix = "find: ",
	enter = function(line) lfm.exec_expr("open") end,
	esc = function() end,
	change = function()
		found = lfm.find(lfm.cmd.line)
		if found then
			lfm.cmd.clear()
			lfm.timeout(250)
			commands.open.f()
		end
	end,
}

local mode_travel = {
	prefix = "travel: ",
	enter = function(line) end,
	esc = function() end,
	change = function()
		found = lfm.find(lfm.cmd.line)
		if found then
			lfm.timeout(250)
			lfm.cmd.line = ""
			if commands.open.f() then
				lfm.cmd.clear()
			end
		end
	end,
}

lfm.register_mode(mode_search)
lfm.register_mode(mode_search_back)
lfm.register_mode(mode_cmd)
lfm.register_mode(mode_filter)
lfm.register_mode(mode_find)
lfm.register_mode(mode_travel)

-- package.loaded.config = nil
-- require("/home/marius/.config/lfm/config.lua")

dofile(config.configpath)
