---@meta

lfm.api = {}

---Clear the command line.
---```lua
---    lfm.api.cmdline_clear()
---```
function lfm.api.cmdline_clear() end

---Delete the character to the left.
---```lua
---    lfm.api.cmdline_delete()
---```
function lfm.api.cmdline_delete() end

---Delete the character to the right.
---```lua
---    lfm.api.cmdline_delete_right()
---```
function lfm.api.cmdline_delete_right() end

---Delete the word to the right.
---```lua
---    lfm.api.cmdline_delete_word()
---```
function lfm.api.cmdline_delete_word() end

---Delete to the beginning of the line.
---```lua
---    lfm.api.cmdline_delete_line_left()
---```
function lfm.api.cmdline_delete_line_left() end

---Move cursor one word left.
---```lua
---    lfm.api.cmdline_word_left()
---```
function lfm.api.cmdline_word_left() end

---Move cursor one word right.
---```lua
---    lfm.api.cmdline_word_right()
---```
function lfm.api.cmdline_word_right() end

---Move cursor to the end.
---```lua
---    lfm.api.cmdline__end()
---```
function lfm.api.cmdline__end() end

---Move cursor to the beginning of the line.
---```lua
---    lfm.api.cmdline_home()
---```
function lfm.api.cmdline_home() end

---Insert a character at the current cursor position.
---```lua
---    lfm.api.cmdline_insert("ö")
---```
---@param c string The character
function lfm.api.cmdline_insert(c) end

---Move the cursor to the left.
---```lua
---    lfm.api.cmdline_left()
---```
function lfm.api.cmdline_left() end

---Move the cursor to the right.
---```lua
---    lfm.api.cmdline_right()
---```
function lfm.api.cmdline_right() end

---Get the current command line string.
---```lua
---    local line = lfm.api.cmdline_line_get()
---    -- do something with line
---```
---@return string
---@nodiscard
function lfm.api.cmdline_line_get() end

---Set the command line. If two arguments are provided.
---The cursor will be positioned between `left` and `right`.
---```lua
---    -- sets the command line to file.txt with the cursor at the end
---    lfm.api.cmdline_line_set("file.txt")
---
---    -- sets the cursor before the dot
---    lfm.api.cmdline_line_set("file", ".txt")
---
---    -- clears:
---    lfm.api.cmdline_line_set()
---```
---@param left? string
---@param right? string
function lfm.api.cmdline_line_set(left, right) end

---Get the current command line prefix.
---```lua
---    local prefix = lfm.api.cmdline_prefix_get()
---    -- do something with prefix
---```
---@return string prefix
---@nodiscard
function lfm.api.cmdline_prefix_get() end

---Toggle between insert and overwrite mode.
---```lua
---    lfm.api.cmdline_toggle_overwrite()
---```
function lfm.api.cmdline_toggle_overwrite() end

---Append a line to history.
---```lua
---    lfm.api.cmdline_history_append(':', "cd /tmp")
---```
---@param prefix string
---@param line string
function lfm.api.cmdline_history_append(prefix, line) end

---Get the next line from history.
---```lua
---    local line = lfm.api.cmdline_history_next()
---    -- do something with line
---```
---@return string
---@nodiscard
function lfm.api.cmdline_history_next() end

---Get the previous line from history.
---```lua
---    local line = lfm.api.cmdline_history_prev()
---    -- do something with line
---```
---@return string
---@nodiscard
function lfm.api.cmdline_history_prev() end

---Get the full history, latest items first
---```lua
---    local history = lfm.api.cmdline_get_history()
---    for i, item in ipairs(history) do
---      print(i, item)
---    end
---```
---@return string
---@nodiscard
function lfm.api.cmdline_get_history() end

---Set the filter string for the current directory. "" or nil clears the filter.
---```lua
---    lfm.api.fm_filter(".txt")
---    lfm.api.fm_filter(".txt", "filter")
---
---    lfm.api.fm_filter("txt", "fuzzy")
---
---    lfm.api.fm_filter(function(name) return string.find(name, "txt") end, "lua")
---
---    lfm.api.fm_filter()
---```
---@param filter? string The filter string.
---@param type? Lfm.FilterType The filter type.
---@overload fun(function: Lfm.FilterFunction, type: "lua")
---@overload fun()
function lfm.api.fm_filter(filter, type) end

---Get the filter string for the current directory.
---```lua
---    local filter = lfm.api.fm_getfilter()
---```
---@return string|nil filter The filter string.
---@return Lfm.FilterType|nil filter The filter type.
---@nodiscard
function lfm.api.fm_getfilter() end

---Jump to the directory saved by the automatic mark (e.g. with '')
---```lua
---    lfm.api.fm_jump_automark()
---```
function lfm.api.fm_jump_automark() end

---Navigate into the directory at the current cursor position. If the current file
---is not a directory, its path is returned instead.
---```lua
---    local path = lfm.api.fm_open()
---    if path then
---      -- path is a file, do something with it
---    else
---      -- we moved into the directory
---    end
---```
---@return string? file Path to the file under the curser, `nil` if it was a directory
function lfm.api.fm_open() end

---Get the current directory.
---```lua
---    local dir = lfm.api.current_dir()
---    print(dir.path)
---    print(dir.name)
---    for i, file in ipairs(dir.files()) do
---      print(i, file)
---    end
---```
---@return Lfm.Dir directory
---@nodiscard
function lfm.api.current_dir() end

---Get the current file.
---```lua
---    local file = lfm.api.current_file()
---    if file then
---      print(file)
---    end
---```
---@return string? file Path to the current file or `nil` if the directory is empty.
---@nodiscard
function lfm.api.current_file() end

---Reverse selection of files in the current directory.
---```lua
---    lfm.api.selection_reverse()
---```
function lfm.api.selection_reverse() end

---Toggle selection of the current file.
---```lua
---    lfm.api.selection_toggle()
---```
function lfm.api.selection_toggle() end

---Add files to the current selection. Must use absolute paths (TODO: make relative paths work)
---```lua
---    lfm.api.selection_add({"/tmp/file1", "/tmp/file2"})
---```
---@param files string[] Table of paths.
function lfm.api.selection_add(files) end

---Set the current selection. Empty table or nil clears.
---```lua
---    lfm.api.selection_set({"/tmp/file1", "/tmp/file2"})
---```
---Clear the selection:
---```lua
---    lfm.api.selection_set({})
---    lfm.api.selection_set()
---```
---@param files? string[] table of strings.
function lfm.api.selection_set(files) end

---Get the current selection. Files are in the order in which they were added.
---```lua
---    local files = lfm.api.selection_get()
---    for i, file in ipairs(files) do
---      print(i, file)
---    end
---```
---@return string[] files table of files as strings.
---@nodiscard
function lfm.api.selection_get() end

---Restore the previous selection. Previous selection is set whenever the selection/paste buffer is cleared.
---```lua
---    lfm.api.selection_restore()
---```
function lfm.api.selection_restore() end

---Flatten the current directory `level`s deep.
---```lua
---    lfm.api.fm_flatten(2)
---```
---@param level integer
function lfm.api.fm_flatten(level) end

---Get the flatten level for the current directory.
---```lua
---    local level = lfm.api.fm_flatten_level()
---    lfm.api.fm_flatten(level + 1)
---```
---@return integer
---@nodiscard
function lfm.api.fm_flatten_level() end

---Set the sort method. Multiple options can be set at once. Later options may override previous ones.
---```lua
---    lfm.api.fm_sort({ type = "ctime", dirfirst = true, reverse = false })
---```
---@param opts Lfm.SortOpts
function lfm.api.fm_sort(opts) end

---Get the info setting for the current directory.
---```lua
---    local info = lfm.api.fm_get_info()
---```
---@return Lfm.Info info
---@nodiscard
function lfm.api.fm_get_info() end

---Set the info setting for the current directory.
---```lua
---    lfm.api.fm_set_info("ctime")
---```
---@param info Lfm.Info
---@return Lfm.Info info
function lfm.api.fm_set_info(info) end

---Change directory to the parent of the current directory, unless in "/".
---```lua
---    lfm.api.fm_updir()
---```
function lfm.api.fm_updir() end

---Get the current paste buffer and mode.
---```lua
---    local files, mode lfm.api.fm_paste_buffer_get()
---    print("mode:", mode)
---    for i, file in ipairs(files) do
---      print(i, file)
---    end
---```
---@return string[] files
---@return Lfm.PasteMode mode
---@nodiscard
function lfm.api.fm_paste_buffer_get() end

---Set the current paste buffer and mode. nil or {} clears the buffer.
---```lua
---    lfm.api.fm_paste_buffer_set({"/tmp/file1", "/tmp/file2"}, "move")
---```
---@param files string[]
---@param mode? Lfm.PasteMode (default: "copy")
function lfm.api.fm_paste_buffer_set(files, mode) end

---Get current paste mode.
---```lua
---    local mode = lfm.api.fm_paste_mode_get()
---    print("mode:", mode)
---```
---@return Lfm.PasteMode mode
---@nodiscard
function lfm.api.fm_paste_mode_get() end

---Set current paste mode without changing the contents of the paste buffer.
---```lua
---    lfm.api.fm_paste_mode_set("move")
---```
---@param mode Lfm.PasteMode (default: `"copy"`)
function lfm.api.fm_paste_mode_set(mode) end

---Add the current selection to the load and change mode to MODE_MOVE.
---```lua
---    lfm.api.fm_cut()
---```
function lfm.api.fm_cut() end

---Add the current selection to the load and change mode to MODE_COPY.
---```lua
---    lfm.api.fm_copy()
---```
function lfm.api.fm_copy() end

---Check the current directory for changes and reload if necessary.
---```lua
---    lfm.api.fm_check()
---```
function lfm.api.fm_check() end

---(Re)load a directory from disk.
---@param path string
---```lua
---    lfm.api.fm_load("/mnt/data")
---```
function lfm.api.fm_load(path) end

---Drop directory cache and reload visible directories from disk.
-- function lfm.api.fm_drop_cache() end
function lfm.api.fm_drop_cache() end

---Reload visible directories from disk.
---```lua
---    lfm.api.fm_reload()
---```
function lfm.api.fm_reload() end

---Move the cursor to a file in the current directory.
---```lua
---    lfm.api.sel("file.txt")
---```
---@param name string
function lfm.api.sel(name) end

---Current height of the file manager, i.e. the maximum number of files shown in one directory.
---```lua
---    local height = lfm.api.fm_get_height()
---    print("height:", height)
---```
---@return integer
---@nodiscard
function lfm.api.fm_get_height() end

---Move the cursor to the bottom.
---```lua
---    lfm.api.fm_bottom()
---```
function lfm.api.fm_bottom() end

---Move the cursor to the top.
---```lua
---    lfm.api.fm_top()
---```
function lfm.api.fm_top() end

---Move the cursor up.
---```lua
---    -- move up one file
---    lfm.api.fm_up()
---
---    -- move up three files
---    lfm.api.fm_up(3)
---```
---@param ct? integer count, 1 if omitted
function lfm.api.fm_up(ct) end

---Move the cursor down.
---```lua
---    -- move down one file
---    lfm.api.fm_down()
---
---    -- move down three files
---    lfm.api.fm_down(3)
---```
---@param ct? integer count, 1 if omitted
function lfm.api.fm_down(ct) end

---Scroll up while keeping the cursor on the current file (if possible).
---```lua
---    lfm.api.fm_scroll_up()
---```
function lfm.api.fm_scroll_up() end

---Scroll down while keeping the cursor on the current file (if possible).
---```lua
---    lfm.api.fm_scroll_down()
---```
function lfm.api.fm_scroll_down() end

---
---Navigate to location given by dir
---
---Example:
---```lua
---    lfm.api.chdir("/home/john")
---
---    lfm.api.chdir("~")
---
---    lfm.api.chdir("../sibling")
---```
---
---@param dir string destination path
---@param force_sync? boolean force chdir immediately
function lfm.api.chdir(dir, force_sync) end

---Get the height in characters of the UI.
---```lua
---    local height = lfm.api.ui_get_height()
---    print("height:", height)
---```
---@return integer height
---@nodiscard
function lfm.api.ui_get_height() end

---Get the width in characters of the UI.
---```lua
---    local width = lfm.api.ui_get_width()
---    print("width:", width)
---```
---@return integer width
---@nodiscard
function lfm.api.ui_get_width() end

---Clear the UI and redraw.
---```lua
---    lfm.api.ui_clear()
---```
function lfm.api.ui_clear() end

---Request redraw. If no `force` is set only parts the need redrawing are drawn.
---Drawing happens _after_ execution of the current lua code finishes and the main
---event loop idles.
---```lua
---    lfm.api.redraw()
---```
---Redraw everything:
---```lua
---    lfm.api.redraw(true)
---```
---@param force? boolean use force (default: `false`)
function lfm.api.redraw(force) end

---Draws a menu on screen.
---```lua
---    lfm.api.ui_menu() -- hide menu
---    lfm.api.ui_menu({"line1", "line2"})
---    lfm.api.ui_menu("line1\nline2")
---```
---@param menu nil|string[]|string
function lfm.api.ui_menu(menu) end

---Show all previously shown errors and messages.
---```lua
---    local messages = lfm.api.ui_messages()
---    for i, message in ipairs(messages) do
---      print(i, message)
---    end
---```
---@return string[] messages
---@nodiscard
function lfm.api.ui_messages() end

---
---Con notcurses open images?
---
---Example:
---```lua
---    local can = lfm.api.notcurses_canopen_images()
---```
---
---@return boolean
---@nodiscard
function lfm.api.notcurses_canopen_images() end

---
---Con notcurses draw images with halfblocks?
---
---Example:
---```lua
---    local can = lfm.api.notcurses_canhalfblock()
---```
---
---@return boolean
---@nodiscard
function lfm.api.notcurses_canhalfblock() end

---
---Can notcurses draw images with quadrants?
---
---Example:
---```lua
---    local can = lfm.api.notcurses_canquadrant()
---```
---
---@return boolean
---@nodiscard
function lfm.api.notcurses_canquadrant() end

---
---Can notcurses draw images with sextants?
---
---Example:
---```lua
---    local can = lfm.api.notcurses_cansextant()
---```
---
---@return boolean
---@nodiscard
function lfm.api.notcurses_cansextant() end

---
---Can notcurses draw images with braille?
---
---Example:
---```lua
---    local can = lfm.api.notcurses_canbraille()
---```
---
---@return boolean
---@nodiscard
function lfm.api.notcurses_canbraille() end

---
---Can notcurses draw pixel perfect bitmaps?
---
---Example:
---```lua
---    local can = lfm.api.notcurses_canpixel()
---```
---
---@return boolean
---@nodiscard
function lfm.api.notcurses_canpixel() end

---
---Can notcurses draw truecolor?
---
---Example:
---```lua
---  local can = lfm.api.notcurses_cantruecolor()
---```
---
---@return boolean
---@nodiscard
function lfm.api.notcurses_cantruecolor() end

---
---Size of the color palette as detected by notcurses.
---
---Example:
---```lua
---  local n = lfm.api.notcurses_palette_size()
---```
---
---@return integer
---@nodiscard
function lfm.api.notcurses_palette_size() end

---Check if a macro is currently being recorded.
---```lua
---    local recording = lfm.api.macro_recording()
---```
---@return boolean
---@nodiscard
function lfm.api.macro_recording() end

---Start recording a macro. Keytrokes are recorded until macro_stop_record is called.
---Returns false if a macro is already being recorded.
---```lua
---    lfm.api.macro_record("a")
---```
---@param id string First found wchar is used as an id.
---@return boolean
function lfm.api.macro_record(id) end

---Stop recording current macro.
---```lua
---    lfm.api.macro_stop_record()
---```
---@return boolean
function lfm.api.macro_stop_record() end

---Replay a macro.
---```lua
---    lfm.api.macro_play("a")
---```
---@param id string First found wchar is used as an id.
---@return boolean
function lfm.api.macro_play(id) end

---
---Get tags of a directory.
---
---Example:
---```lua
---  local t, cols = lfm.api.get_tags("/home/john")
---```
---
---@param path string
---@return string[]
---@return integer
function lfm.api.get_tags(path) end

---
---Set tags for a directory. Only works for directories that have already been loaded.
---
---Example:
---```lua
---  local ok = lfm.api.set_tags("/home/john", { ["file.txt"] = "X" }, 1)
---```
---
---```lua
---  local ok = lfm.api.set_tags("/home/john", nil) -- nil is explicit here
---```
---
---@param path string path to the directory
---@param tags table<string,string>|nil a map of filenames -> tag, `nil` clears and sets cols to `0`
---@param cols? integer number of columns (in characters) to print, if `nil`, leave as is
---@return boolean ok `true` on success, `false` if the directory wasn't loaded
function lfm.api.set_tags(path, tags, cols) end

---
---Get the list of paths of cached directories.
---
---Example:
---```lua
---  local t = lfm.api.get_cached_dirs()
---```
---
---@return string[]
function lfm.api.get_cached_dirs() end
