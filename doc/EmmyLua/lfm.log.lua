---@meta

---@class Lfm.Log
---@field level number (assignable)
---@field TRACE number
---@field DEBUG number
---@field INFO number
---@field WARN number
---@field ERROR number
---@field FATAL number
lfm.log = {}

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
