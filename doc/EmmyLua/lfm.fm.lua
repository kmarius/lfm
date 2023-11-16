---@meta

lfm.fm = {}

---@class Lfm.Dir
---@field path string
---@field name string
---@field files table[string] table of filenames

---@alias Lfm.SortOption
---| '"name"'
---| '"natural"'
---| '"ctime"'
---| '"mtime"'
---| '"atime"'
---| '"size"'
---| '"random"'
---| '"dirfirst"'
---| '"nodirfirst"'
---| '"reverse"'
---| '"noreverse"'

---@alias Lfm.PasteMode
---| '"copy"'
---| '"move"'

---@alias Lfm.Info
---| '"size"'
---| '"atime"'
---| '"ctime"'
---| '"mtime"'

---@alias Lfm.FilterType
---| '"filter"'
---| '"fuzzy"'
---| '"lua"'

---@alias Lfm.FilterFunction fun(name: string):any

---Set the filter string for the current directory. "" or nil clears the filter.
---```lua
---    lfm.fm.filter(".txt")
---    lfm.fm.filter(".txt", "filter")
---
---    lfm.fm.filter("txt", "fuzzy")
---
---    lfm.fm.filter(function(name) return string.find(name, "txt") end, "lua")
---
---    lfm.fm.filter()
---```
---@param filter? string The filter string.
---@param type? Lfm.FilterType The filter type.
---@overload fun(function: Lfm.FilterFunction, type: "lua")
---@overload fun()
function lfm.fm.filter(filter, type) end

---Get the filter string for the current directory.
---```lua
---    local filter = lfm.fm.getfilter()
---```
---@return string|nil filter The filter string.
---@return Lfm.FilterType|nil filter The filter type.
---@nodiscard
function lfm.fm.getfilter() end

---Jump to the directory saved by the automatic mark (e.g. with '')
---```lua
---    lfm.fm.jump_automark()
---```
function lfm.fm.jump_automark() end

---Navigate into the directory at the current cursor position. If the current file
---is not a directory, its path is returned instead.
---```lua
---    local path = lfm.fm.open()
---    if path then
---      -- path is a file, do something with it
---    else
---      -- we moved into the directory
---    end
---```
---@return string? file Path to the file under the curser, `nil` if it was a directory
function lfm.fm.open() end

---Get the current directory.
---```lua
---    local dir = lfm.fm.current_dir()
---    print(dir.path)
---    print(dir.name)
---    for i, file in ipairs(dir.files) do
---      print(i, file)
---    end
---```
---@return Lfm.Dir directory
---@nodiscard
function lfm.fm.current_dir() end

---Get the current file.
---```lua
---    local file = lfm.fm.current_file()
---    if file then
---      print(file)
---    end
---```
---@return string? file Path to the current file or `nil` if the directory is empty.
---@nodiscard
function lfm.fm.current_file() end

---Reverse selection of files in the current directory.
---```lua
---    lfm.fm.selection_reverse()
---```
function lfm.fm.selection_reverse() end

---Toggle selection of the current file.
---```lua
---    lfm.fm.selection_toggle()
---```
function lfm.fm.selection_toggle() end

---Add files to the current selection. Must use absolute paths (TODO: make relative paths work)
---```lua
---    lfm.fm.selection_add({"/tmp/file1", "/tmp/file2"})
---```
---@param files string[] Table of paths.
function lfm.fm.selection_add(files) end

---Set the current selection. Empty table or nil clears.
---```lua
---    lfm.fm.selection_set({"/tmp/file1", "/tmp/file2"})
---```
---Clear the selection:
---```lua
---    lfm.fm.selection_set({})
---    lfm.fm.selection_set()
---```
---@param files? string[] table of strings.
function lfm.fm.selection_set(files) end

---Get the current selection. Files are in the order in which they were added.
---```lua
---    local files = lfm.fm.selection_get()
---    for i, file in ipairs(files) do
---      print(i, file)
---    end
---```
---@return string[] files table of files as strings.
---@nodiscard
function lfm.fm.selection_get() end

---Flatten the current directory `level`s deep.
---```lua
---    lfm.fm.flatten(2)
---```
---@param level integer
function lfm.fm.flatten(level) end

---Get the flatten level for the current directory.
---```lua
---    local level = lfm.fm.flatten_level()
---    lfm.fm.flatten(level + 1)
---```
---@return integer
---@nodiscard
function lfm.fm.flatten_level() end

---Set the sort method. Multiple options can be set at once. Later options may override previous ones.
---```lua
---    lfm.fm.sortby("ctime", "nodirfirst", "reverse")
---```
---@param ... Lfm.SortOption
function lfm.fm.sortby(...) end

---Get the info setting for the current directory.
---```lua
---    local info = lfm.fm.get_info()
---```
---@return Lfm.Info info
---@nodiscard
function lfm.fm.get_info() end

---Set the info setting for the current directory.
---```lua
---    lfm.fm.set_info("ctime")
---```
---@param info Lfm.Info
---@return Lfm.Info info
function lfm.fm.set_info(info) end

---Change directory to the parent of the current directory, unless in "/".
---```lua
---    lfm.fm.updir()
---```
function lfm.fm.updir() end

---Get the current paste buffer and mode.
---```lua
---    local files, mode lfm.fm.paste_buffer_get()
---    print("mode:", mode)
---    for i, file in ipairs(files) do
---      print(i, file)
---    end
---```
---@return string[] files
---@return Lfm.PasteMode mode
---@nodiscard
function lfm.fm.paste_buffer_get() end

---Set the current paste buffer and mode. nil or {} clears the buffer.
---```lua
---    lfm.fm.paste_buffer_set({"/tmp/file1", "/tmp/file2"}, "move")
---```
---@param files string[]
---@param mode? Lfm.PasteMode (default: "copy")
function lfm.fm.paste_buffer_set(files, mode) end

---Get current paste mode.
---```lua
---    local mode = lfm.fm.paste_mode_get()
---    print("mode:", mode)
---```
---@return Lfm.PasteMode mode
---@nodiscard
function lfm.fm.paste_mode_get() end

---Set current paste mode without changing the contents of the paste buffer.
---```lua
---    lfm.fm.paste_mode_set("move")
---```
---@param mode Lfm.PasteMode (default: `"copy"`)
function lfm.fm.paste_mode_set(mode) end

---Add the current selection to the load and change mode to MODE_MOVE.
---```lua
---    lfm.fm.cut()
---```
function lfm.fm.cut() end

---Add the current selection to the load and change mode to MODE_COPY.
---```lua
---    lfm.fm.copy()
---```
function lfm.fm.copy() end

---Check the current directory for changes and reload if necessary.
---```lua
---    lfm.fm.check()
---```
function lfm.fm.check() end

---(Re)load a directory from disk.
---@param path string
---```lua
---    lfm.fm.load("/mnt/data")
---```
function lfm.fm.load(path) end

---Drop directory cache and reload visible directories from disk.
-- function lfm.fm.drop_cache() end

---Reload visible directories from disk.
---```lua
---    lfm.fm.reload()
---```
function lfm.fm.reload() end

---Move the cursor to a file in the current directory.
---```lua
---    lfm.fm.sel("file.txt")
---```
---@param name string
function lfm.fm.sel(name) end

---Current height of the file manager, i.e. the maximum number of files shown in one directory.
---```lua
---    local height = lfm.fm.get_height()
---    print("height:", height)
---```
---@return integer
---@nodiscard
function lfm.fm.get_height() end

---Move the cursor to the bottom.
---```lua
---    lfm.fm.bottom()
---```
function lfm.fm.bottom() end

---Move the cursor to the top.
---```lua
---    lfm.fm.top()
---```
function lfm.fm.top() end

---Move the cursor up.
---```lua
---    -- move up one file
---    lfm.fm.up()
---
---    -- move up three files
---    lfm.fm.up(3)
---```
---@param ct? integer count, 1 if omitted
function lfm.fm.up(ct) end

---Move the cursor down.
---```lua
---    -- move down one file
---    lfm.fm.down()
---
---    -- move down three files
---    lfm.fm.down(3)
---```
---@param ct? integer count, 1 if omitted
function lfm.fm.down(ct) end

---Scroll up while keeping the cursor on the current file (if possible).
---```lua
---    lfm.fm.scroll_up()
---```
function lfm.fm.scroll_up() end

---Scroll down while keeping the cursor on the current file (if possible).
---```lua
---    lfm.fm.scroll_down()
---```
function lfm.fm.scroll_down() end

---Navigate to location given by dir
---```lua
---    lfm.fm.chdir("/home/john")
---
---    lfm.fm.chdir("~")
---
---    lfm.fm.chdir("../sibling")
---```
---@param dir string destination path
function lfm.fm.chdir(dir) end
