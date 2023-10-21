---@meta

---@alias Lfm.Log.Level integer

---@class Lfm.Log
---@field TRACE Lfm.Log.Level
---@field DEBUG Lfm.Log.Level
---@field INFO Lfm.Log.Level
---@field WARN Lfm.Log.Level
---@field ERROR Lfm.Log.Level
---@field FATAL Lfm.Log.Level
lfm.log = {}

---Get the log level
---@return Lfm.Log.Level level
function lfm.log.get_level() end

---Set the log level
---@param level Lfm.Log.Level
function lfm.log.set_level(level) end

---@param ... any
function lfm.log.trace(...) end

---@param ... any
function lfm.log.debug(...) end

---@param ... any
function lfm.log.info(...) end

---@param ... any
function lfm.log.warn(...) end

---@param ... any
function lfm.log.error(...) end

---@param ... any
function lfm.log.fatal(...) end

---@param fmt string
---@param ... any
function lfm.log.tracef(fmt, ...) end

---@param fmt string
---@param ... any
function lfm.log.debugf(fmt, ...) end

---@param fmt string
---@param ... any
function lfm.log.infof(fmt, ...) end

---@param fmt string
---@param ... any
function lfm.log.warnf(fmt, ...) end

---@param fmt string
---@param ... any
function lfm.log.errorf(fmt, ...) end

---@param fmt string
---@param ... any
function lfm.log.fatalf(fmt, ...) end
