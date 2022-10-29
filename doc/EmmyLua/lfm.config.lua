---@meta

---@class DirSetting
---@field sorttype sortoption
---@field dirfirst boolean
---@field reverse boolean
---@field hidden boolean

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
---@field histsize number history size, must be non-negative (default: 100)
---@field infoline string|nil infoline string
---@field threads number number of threads in the pool (at least 2, default: nprocs+1)
---@field dir_settings table<path, DirSetting>
---@field ratios integer[] assignable
---@field truncatechar string assignable, only the first character is used
---@field scrolloff integer assignable
---@field hidden boolean assignable
---@field preview boolean assignable
---@field preview_images boolean assignable
---@field previewer string assignable
---@field icons boolean assignable
---@field icon_map table<string, string> assignable
---@field configpath string
---@field configdir string
---@field luadir string
---@field datadir string
---@field statedir string
---@field runtime_dir string
---@field logpath string
---@field fifopath string
---@field colors ColorLib
---@field inotify_blacklist string[] No inotify watchers will be installed if the path begins with any of these strings.
---@field inotify_timeout number Minimum time in milliseconds between reloads triggered by inotify. Must larger or equal to 100.
---@field inotify_delay number Small delay in milliseconds before relloads are triggered by inotify.
lfm.config = {}
