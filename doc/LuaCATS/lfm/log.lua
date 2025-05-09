---@meta

---@enum Log.Level
local LogLevel = {
	TRACE = 0,
	DEBUG = 1,
	INFO = 2,
	WARN = 3,
	ERROR = 4,
	FATAL = 5,
}

---@class Lfm.Log
---@field TRACE Log.Level # 0
---@field DEBUG Log.Level # 1
---@field INFO Log.Level # 2
---@field WARN Log.Level # 3
---@field ERROR Log.Level # 4
---@field FATAL Log.Level # 5
---@field level Log.Level Get/set the current log level.
lfm.log = {}

---Get the log level.
---```lua
---    local level = lfm.log.get_level()
---```
----@return Lfm.Log.Level level
---@return Log.Level level
---@nodiscard
function lfm.log.get_level() end

---Set the log level.
---```lua
---    lfm.log.set_level(lfm.log.TRACE)
---```
---@param level Log.Level
function lfm.log.set_level(level) end

---Log one or more objects.
---```lua
---    lfm.log.trace("logging..")
---```
---@param ... any
function lfm.log.trace(...) end

---Log one or more objects.
---```lua
---    lfm.log.trace("logging..")
---```
---@param ... any
function lfm.log.debug(...) end

---Log one or more objects.
---```lua
---    lfm.log.trace("logging..")
---```
---@param ... any
function lfm.log.info(...) end

---Log one or more objects.
---```lua
---    lfm.log.trace("logging..")
---```
---@param ... any
function lfm.log.warn(...) end

---Log one or more objects.
---```lua
---    lfm.log.trace("logging..")
---```
---@param ... any
function lfm.log.error(...) end

---Log one or more objects.
---```lua
---    lfm.log.trace("logging..")
---```
---@param ... any
function lfm.log.fatal(...) end

---Log a formatted string.
---```lua
---    lfm.log.tracef("%s: logging..", "module.name")
---```
---@param fmt string
---@param ... any
function lfm.log.tracef(fmt, ...) end

---Log a formatted string.
---```lua
---    lfm.log.debugf("%s: logging..", "module.name")
---```
---@param fmt string
---@param ... any
function lfm.log.debugf(fmt, ...) end

---Log a formatted string.
---```lua
---    lfm.log.infof("%s: logging..", "module.name")
---```
---@param fmt string
---@param ... any
function lfm.log.infof(fmt, ...) end

---Log a formatted string.
---```lua
---    lfm.log.warnf("%s: logging..", "module.name")
---```
---@param fmt string
---@param ... any
function lfm.log.warnf(fmt, ...) end

---Log a formatted string.
---```lua
---    lfm.log.errorf("%s: logging..", "module.name")
---```
---@param fmt string
---@param ... any
function lfm.log.errorf(fmt, ...) end

---Log a formatted string.
---```lua
---    lfm.log.fatalf("%s: logging..", "module.name")
---```
---@param fmt string
---@param ... any
function lfm.log.fatalf(fmt, ...) end
