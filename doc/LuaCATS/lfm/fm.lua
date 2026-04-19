---@meta

---@exact
lfm.fm = {}

---@alias Lfm.FilterFunction fun(name: string):any

---
---Set the filter string for the current directory. "" or nil clears the filter.
---
---Example:
---```lua
---  lfm.fm.set_filter(".txt")
---  lfm.fm.set_filter(".txt", "filter")
---
---  lfm.fm.set_filter("txt", "fuzzy")
---
---  lfm.fm.set_filter(function(name) return string.find(name, "txt") end, "lua")
---
---  lfm.fm.set_filter()
---```
---
---@param filter? string The filter string.
---@param type? Lfm.FilterType The filter type.
---@overload fun(function: Lfm.FilterFunction, type: "lua")
---@overload fun()
function lfm.fm.set_filter(filter, type) end

---
---Get the filter string for the current directory.
---
---Example:
---```lua
---    local filter = lfm.fm.get_filter()
---```
---
---@return string|nil filter The filter string.
---@return Lfm.FilterType|nil filter The filter type.
---@nodiscard
function lfm.fm.get_filter() end

---
---Jump to the directory saved by the automatic mark (e.g. with '')
---
---Example:
---```lua
---  lfm.fm.jump_automark()
---```
---
function lfm.fm.jump_automark() end

---
---Get the location automatic quickmark.
---
---Example:
---```lua
---  local path = lfm.fm.get_automark()
---```
---
function lfm.fm.get_automark() end

---
---Navigate into the directory at the current cursor position. If the current file
---is not a directory, its path is returned instead.
---
---Example:
---```lua
---  local path = lfm.fm.open()
---  if path then
---    -- path is a file, do something with it
---  else
---    -- we moved into the directory
---  end
---```
---
---@return string? file Path to the file under the curser, `nil` if it was a directory
function lfm.fm.open() end

---
---Get the current directory.
---
---Example:
---```lua
---  local dir = lfm.fm.current_dir()
---  print(dir.path)
---  print(dir.name)
---  for i, file in ipairs(dir.files()) do
---    print(i, file)
---  end
---```
---
---@return Lfm.Dir directory
---@nodiscard
function lfm.fm.current_dir() end

---
---Get the current file.
---
---Example:
---```lua
---  local file = lfm.fm.current_file()
---  if file then
---    print(file)
---  end
---```
---
---@return string? file Path to the current file or `nil` if the directory is empty.
---@nodiscard
function lfm.fm.current_file() end

---
---Reverse selection of files in the current directory.
---
---Example:
---```lua
---  lfm.fm.reverse_selection()
---```
---
function lfm.fm.reverse_selection() end

---
---Toggle selection of the current file.
---
---Example:
---```lua
---  lfm.fm.toggle_selection()
---```
---
function lfm.fm.toggle_selection() end

---
---Add files to the current selection. Must use absolute paths (TODO: make relative paths work)
---
---Example:
---```lua
---  lfm.fm.add_selection({"/tmp/file1", "/tmp/file2"})
---```
---
---@param files string[] Table of paths.
function lfm.fm.add_selection(files) end

---
---Set the current selection. Empty table or nil clears.
---
---Example:
---```lua
---  lfm.fm.set_selection({"/tmp/file1", "/tmp/file2"})
---```
---Clear the selection:
---```lua
---  lfm.fm.set_selection({})
---  lfm.fm.set_selection()
---```
---
---@param files? string[] table of strings.
function lfm.fm.set_selection(files) end

---
---Get the current selection. Files are in the order in which they were added.
---
---Example:
---```lua
---  local files = lfm.fm.get_selection()
---  for i, file in ipairs(files) do
---    print(i, file)
---  end
---```
---
---@return string[] files table of files as strings.
---@nodiscard
function lfm.fm.get_selection() end

---
---Restore the previous selection. Previous selection is set whenever the selection/paste buffer is cleared.
---
---Example:
---```lua
---  lfm.fm.restore_selection()
---```
---
function lfm.fm.restore_selection() end

---
---Flatten the current directory `level`s deep.
---
---Example:
---```lua
---  lfm.fm.set_flatten_level(2)
---```
---
---@param level integer
function lfm.fm.set_flatten_level(level) end

---
---Get the flatten level for the current directory.
---
---Example:
---```lua
---  local level = lfm.fm.get_flatten_level()
---  lfm.fm.set_flatten_level(level + 1)
---```
---
---@return integer
---@nodiscard
function lfm.fm.get_flatten_level() end

---
---Set the sort method. Multiple options can be set at once. Later options may override previous ones.
---
---Example:
---```lua
---  lfm.fm.sort({ type = "ctime", dirfirst = true, reverse = false })
---```
---
---Sort using a lua function to extract a key from the file name:
---```lua
---  lfm.fm.sort({
---  	keyfunc = function(name)
---  		local m = string.match(name, "%(([0-9]+)%)$")
---  		if not m then
---  			return 0
---  		end
---  		return tonumber(m)
---  	end,
---  })
---```
---
---@param opts Lfm.SortOpts
function lfm.fm.sort(opts) end

---
---Get the info setting for the current directory.
---
---Example:
---```lua
---  local info = lfm.fm.get_info()
---```
---
---@return Lfm.Info info
---@nodiscard
function lfm.fm.get_info() end

---
---Set the info setting for the current directory.
---
---Example:
---```lua
---  lfm.fm.set_info("ctime")
---```
---
---@param info Lfm.Info
---@return Lfm.Info info
function lfm.fm.set_info(info) end

---
---Change directory to the parent of the current directory, unless in "/".
---
---Example:
---```lua
---  lfm.fm.updir()
---```
---
function lfm.fm.updir() end

---
---Get the current paste buffer and mode.
---
---Example:
---```lua
---  local files, mode lfm.fm.get_paste_buffer()
---  print("mode:", mode)
---  for i, file in ipairs(files) do
---    print(i, file)
---  end
---```
---
---@return string[] files
---@return Lfm.PasteMode mode
---@nodiscard
function lfm.fm.get_paste_buffer() end

---
---Set the current paste buffer and mode. nil or {} clears the buffer.
---
---Example:
---```lua
---  lfm.fm.set_paste_buffer({"/tmp/file1", "/tmp/file2"}, "move")
---```
---
---@param files string[]
---@param mode? Lfm.PasteMode (default: "copy")
function lfm.fm.set_paste_buffer(files, mode) end

---
---Get current paste mode.
---
---Example:
---```lua
---  local mode = lfm.fm.get_paste_mode()
---  print("mode:", mode)
---```
---
---@return Lfm.PasteMode mode
---@nodiscard
function lfm.fm.get_paste_mode() end

---
---Set current paste mode without changing the contents of the paste buffer.
---
---Example:
---```lua
---  lfm.fm.set_paste_mode("move")
---```
---
---@param mode Lfm.PasteMode (default: `"copy"`)
function lfm.fm.set_paste_mode(mode) end

---
---Add the current selection to the load and change mode to MODE_MOVE.
---
---Example:
---```lua
---  lfm.fm.cut()
---```
---
function lfm.fm.cut() end

---
---Add the current selection to the load and change mode to MODE_COPY.
---
---Example:
---```lua
---  lfm.fm.copy()
---```
---
function lfm.fm.copy() end

---
---Check the current directory for changes and reload if necessary.
---
---Example:
---```lua
---  lfm.fm.check()
---```
---
function lfm.fm.check() end

---
---(Re)load a directory from disk.
---
---Example:
---```lua
---  lfm.fm.load("/mnt/data")
---```
---
---@param path string
function lfm.fm.load(path) end

---Drop directory cache and reload visible directories from disk.
function lfm.fm.drop_cache() end

---
---Reload visible directories from disk.
---
---Example:
---```lua
---  lfm.fm.reload()
---```
---
function lfm.fm.reload() end

---
---Move the cursor to a file in the current directory.
---
---Example:
---```lua
---  lfm.fm.sel("file.txt")
---```
---
---@param name string
function lfm.fm.select(name) end

---
---Current height of the file manager, i.e. the maximum number of files shown in one directory.
---
---Example
---```lua
---  local height = lfm.fm.get_height()
---  print("height:", height)
---```
---
---@return integer
---@nodiscard
function lfm.fm.get_height() end

---
---Move the cursor to the bottom.
---
---Example:
---```lua
---  lfm.fm.bottom()
---```
---
function lfm.fm.bottom() end

---
---Move the cursor to the top.
---
---Example:
---```lua
---  lfm.fm.top()
---```
---
function lfm.fm.top() end

---
---Move the cursor up.
---
---Example:
---```lua
---  -- move up one file
---  lfm.fm.up()
---
---  -- move up three files
---  lfm.fm.up(3)
---```
---
---@param ct? integer
function lfm.fm.up(ct) end

---
---Move the cursor down.
---
---Example:
---```lua
---  -- move down one file
---  lfm.fm.down()
---
---  -- move down three files
---  lfm.fm.down(3)
---```
---
---@param ct? integer
function lfm.fm.down(ct) end

---
---Scroll up while keeping the cursor on the current file (if possible).
---
---Example:
---```lua
---  lfm.fm.scroll_up()
---```
---
---@param ct? integer
function lfm.fm.scroll_up(ct) end

---
---Scroll down while keeping the cursor on the current file (if possible).
---
---Example:
---```lua
---  lfm.fm.scroll_down()
---```
---
---@param ct? integer
function lfm.fm.scroll_down(ct) end

---
---Navigate to location given by dir
---
---Example:
---```lua
---  lfm.fm.chdir("/home/john")
---
---  lfm.fm.chdir("~")
---
---  lfm.fm.chdir("../sibling")
---```
---
---@param dir string destination path
---@param force_sync? boolean force chdir immediately
function lfm.fm.chdir(dir, force_sync) end

---
---Get the current working directory of the file manager.
---
---Example:
---```lua
---  local pwd = lfm.fm.getpwd()
---```
---
---@return string
---@nodiscard
function lfm.fm.getpwd() end
