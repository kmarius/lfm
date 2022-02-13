---@meta

---@alias Color string|integer

---@class ColorPair
---@field fg Color
---@field bg Color

---@class ExtColor
---@field ext string[]
---@field color ColorPair

---@class ColorLib
---@field patterns ExtColor[]
---@field copy ColorPair
---@field delete ColorPair
---@field dir ColorPair
---@field broken ColorPair
---@field exec ColorPair
---@field search ColorPair
---@field normal ColorPair
---@field current ColorPair

---@class configlib
---@field ratios integer[] assignable
---@field truncatechar string assignable, only the first character is used
---@field scrolloff integer assignable
---@field hidden boolean assignable
---@field preview boolean assignable
---@field previewer string assignable
---@field configpath string
---@field luadir string
---@field datadir string
---@field user_datadir string
---@field logpath string
---@field fifopath string
---@field dircache_size integer assignable
---@field previewcache_size integer assignable
---@field colors ColorLib
lfm.config = {}
