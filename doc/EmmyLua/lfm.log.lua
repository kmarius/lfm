---@meta


---@class Lfm.Log
---@field level number (assignable)
---@field LOG_TRACE number
---@field LOG_DEBUG number
---@field LOG_INFO number
---@field LOG_WARN number
---@field LOG_ERROR number
---@field LOG_FATAL number
lfm.log = {}

---@vararg any
function lfm.log.trace(...) end

---@vararg any
function lfm.log.debug(...) end

---@vararg any
function lfm.log.info(...) end

---@vararg any
function lfm.log.warn(...) end

---@vararg any
function lfm.log.error(...) end

---@vararg any
function lfm.log.fatal(...) end
