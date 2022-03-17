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
---@field runtime_dir string
---@field logpath string
---@field fifopath string
---@field dircache_size integer assignable
---@field previewcache_size integer assignable
---@field colors ColorLib
---@field inotify_blacklist string[] No inotify watchers will be installed if the path begins with any of these strings.
---@field inotify_timeout number Minimum time in milliseconds between reloads triggered by inotify. Must larger or equal to 100.
---@field inotify_delay number Small delay in milliseconds before relloads are triggered by inotify.
lfm.config = {}
