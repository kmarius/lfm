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
