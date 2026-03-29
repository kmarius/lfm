---@meta

lfm.api = {}

---@class Lfm.MapOpts
---@field desc? string Description of the mapping
---@field mode? string Name of the mode for a mode-only mapping

---@class Lfm.DelMapOpts
---@field mode? string Name of the mode.

---
---Map a key sequence to a function in normal mode, unles specified via opts. The function is called with
---the command repetition count if it greater than 0 or nil if not.
---
---### Basic usage:
---```lua
---  lfm.api.set_keymap("P", function()
---    print("hey")
---  end, { desc = "My cool mapping" })
---```
---
---### Mode mappings:
---```lua
---  -- Only active "my-mode"
---  lfm.api.set_keymap("P", function()
---    print("hey")
---  end, { desc = "My cool mapping", mode = "my-mode" })
---```
---
---### Accepting a command count:
---```lua
---  -- typing 2P prints "count: 2"
---  lfm.api.set_keymap("P", function(ct)
---     if ct then
---       print("count: " .. ct)
---     else
---       print("no count given")
---     end
---  end, { desc = "My cool mapping" })
---```
---
---@param lhs string
---@param rhs function
---@param opts? Lfm.MapOpts
function lfm.api.set_keymap(lhs, rhs, opts) end

---
---Delete a keymap.
---
---Example:
---```lua
---  lfm.del_keymap("<c-d>", { mode = "command" })
---```
---
---@param lhs string
---@param opts? Lfm.DelMapOpts
function lfm.api.del_keymap(lhs, opts) end

---@class Lfm.Keymap
---@field desc string
---@field lhs string
---@field rhs function

---
---Get a table of all maps for a mode. If the `prune` parameter is set,
---only reachable maps are returned, i.e. if both `"g"` and `"gn"` are mapped,
---`"gn"` is not reachable and therefore not included.
---
---```lua
---  local maps = lfm.api.get_keymap("normal")
---  for _, map in pairs(maps) do
---    print(map.keys, map.desc)
---    local f = map.f -- holds the function
---  end
---```
---
---@param mode string
---@param prune? boolean list reachable maps only (default: `false`)
---@return Lfm.Keymap[]
---@nodiscard
function lfm.api.get_keymap(mode, prune) end

---
---Send keys to the input buffer. The input buffer is handled asynchronuously in the event loop.
---
---Example:
---```lua
---  lfm.api.input(":mkdir ")
---```
---
---@param keys string
function lfm.api.input(keys) end

---
---Handle a key sequence immediately, bypassing the input buffer.
---
---Example:
---```lua
---  -- select the first three files
---  lfm.api.feedkeys("ggV2jV")
---
---  -- Type a command into the command line and execute it
---  lfm.api.feedkeys(":cd ~<Enter>")
---```
---
---@param keys string
function lfm.api.feedkeys(keys) end

---
---Clear the command line.
---
---Example:
---```lua
---  lfm.api.cmdline_clear()
---```
---
function lfm.api.cmdline_clear() end

---
---Delete the character to the left.
---
---Example:
---```lua
---  lfm.api.cmdline_delete()
---```
---
function lfm.api.cmdline_delete() end

---
---Delete the character to the right.
---
---Example:
---```lua
---  lfm.api.cmdline_delete_right()
---```
---
function lfm.api.cmdline_delete_right() end

---
---Delete the word to the right.
---
---Example:
---```lua
---  lfm.api.cmdline_delete_word()
---```
---
function lfm.api.cmdline_delete_word() end

---
---Delete to the beginning of the line.
---
---Example:
---```lua
---  lfm.api.cmdline_delete_line_left()
---```
---
function lfm.api.cmdline_delete_line_left() end

---
---Move cursor one word left.
---
---Example:
---```lua
---  lfm.api.cmdline_word_left()
---```
---
function lfm.api.cmdline_word_left() end

---
---Move cursor one word right.
---
---Example:
---```lua
---  lfm.api.cmdline_word_right()
---```
---
function lfm.api.cmdline_word_right() end

---
---Move cursor to the end.
---
---Example:
---```lua
---  lfm.api.cmdline__end()
---```
---
function lfm.api.cmdline__end() end

---
---Move cursor to the beginning of the line.
---
---Example:
---```lua
---  lfm.api.cmdline_home()
---```
---
function lfm.api.cmdline_home() end

---
---Insert a character at the current cursor position.
---
---Example:
---```lua
---  lfm.api.cmdline_insert("ö")
---```
---
---@param c string The character
function lfm.api.cmdline_insert(c) end

---
---Move the cursor to the left.
---
---Example:
---```lua
---  lfm.api.cmdline_left()
---```
---
function lfm.api.cmdline_left() end

---
---Move the cursor to the right.
---
---Example:
---```lua
---  lfm.api.cmdline_right()
---```
---
function lfm.api.cmdline_right() end

---
---Get the current command line string.
---
---Example:
---```lua
---  local line = lfm.api.cmdline_line_get()
---  -- do something with line
---```
---
---@return string
---@nodiscard
function lfm.api.cmdline_line_get() end

---
---Set the command line. If two arguments are provided.
---The cursor will be positioned between `left` and `right`.
---
---Example:
---```lua
---  -- sets the command line to file.txt with the cursor at the end
---  lfm.api.cmdline_line_set("file.txt")
---
---  -- sets the cursor before the dot
---  lfm.api.cmdline_line_set("file", ".txt")
---
---  -- clears:
---  lfm.api.cmdline_line_set()
---```
---
---@param left? string
---@param right? string
function lfm.api.cmdline_line_set(left, right) end

---
---Get the current command line prefix.
---
---Example:
---```lua
---  local prefix = lfm.api.cmdline_prefix_get()
---  -- do something with prefix
---```
---
---@return string prefix
---@nodiscard
function lfm.api.cmdline_prefix_get() end

---
---Toggle between insert and overwrite mode.
---
---Example:
---```lua
---  lfm.api.cmdline_toggle_overwrite()
---```
---
function lfm.api.cmdline_toggle_overwrite() end

---
---Append a line to history.
---
---Example:
---```lua
---  lfm.api.cmdline_history_append(':', "cd /tmp")
---```
---
---@param prefix string
---@param line string
function lfm.api.cmdline_history_append(prefix, line) end

---
---Get the next line from history.
---
---Example:
---```lua
---  local line = lfm.api.cmdline_history_next()
---  -- do something with line
---```
---
---@return string
---@nodiscard
function lfm.api.cmdline_history_next() end

---
---Get the previous line from history.
---
---Example:
---```lua
---  local line = lfm.api.cmdline_history_prev()
---  -- do something with line
---```
---
---@return string
---@nodiscard
function lfm.api.cmdline_history_prev() end

---
---Get the full history, latest items first
---
---Example:
---```lua
---  local history = lfm.api.cmdline_get_history()
---  for i, item in ipairs(history) do
---    print(i, item)
---  end
---```
---
---@return string
---@nodiscard
function lfm.api.cmdline_get_history() end

---
---Get the height in characters of the UI.
---
---Example
---```lua
---  local height = lfm.api.ui_get_height()
---  print("height:", height)
---```
---
---@return integer height
---@nodiscard
function lfm.api.ui_get_height() end

---
---Get the width in characters of the UI.
---
---Example:
---```lua
---  local width = lfm.api.ui_get_width()
---  print("width:", width)
---```
---
---@return integer width
---@nodiscard
function lfm.api.ui_get_width() end

---
---Clear the UI and redraw.
---
---Example:
---```lua
---  lfm.api.ui_clear()
---```
---
function lfm.api.ui_clear() end

---
---Request redraw. If no `force` is set only parts the need redrawing are drawn.
---Drawing happens _after_ execution of the current lua code finishes and the main
---event loop idles.
---
---Example:
---```lua
---  lfm.api.redraw()
---```
---
---Redraw everything:
---```lua
---  lfm.api.redraw(true)
---```
---
---@param force? boolean use force (default: `false`)
function lfm.api.redraw(force) end

---
---Draws a menu on screen.
---
---Example:
---```lua
---  lfm.api.ui_menu() -- hide menu
---  lfm.api.ui_menu({"line1", "line2"})
---  lfm.api.ui_menu("line1\nline2")
---```
---
---@param menu nil|string[]|string
function lfm.api.ui_menu(menu) end

---
---Show all previously shown errors and messages.
---
---Example:
---```lua
---  local messages = lfm.api.get_messages()
---  for i, message in ipairs(messages) do
---    print(i, message)
---  end
---```
---
---@return string[] messages
---@nodiscard
function lfm.api.get_messages() end

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

---
---Check if a macro is currently being recorded and get its identifier.
---
---Example:
---```lua
---  local recording = lfm.api.macro_recording()
---```
---
---@return string|nil identifier (key) for the current macro or nil
---@nodiscard
function lfm.api.macro_recording() end

---
---Start recording a macro. Keytrokes are recorded until macro_stop_record is called.
---Throws an error if a macro is already being recorded.
---
---Example:
---```lua
---  lfm.api.macro_record("a")
---```
---
---@param id string First found wchar is used as an id.
function lfm.api.macro_record(id) end

---
---Stop recording current macro.
---
---Example:
---```lua
---  lfm.api.macro_stop_record()
---```
---
---@return boolean
function lfm.api.macro_stop_record() end

---
---Replay a macro.
---
---Example:
---```lua
---  lfm.api.macro_play("a")
---```
---
---@param id string First found wchar is used as an id.
---@return boolean
function lfm.api.macro_play(id) end

---
---Get tags of a directory.
---
---Example:
---```lua
---  local t, cols = lfm.api.get_directory_tags("/home/john")
---```
---
---@param path string
---@return string[]
---@return integer
function lfm.api.get_directory_tags(path) end

---
---Set tags for a directory. Only works for directories that have already been loaded.
---
---Example:
---```lua
---  local ok = lfm.api.set_directory_tags("/home/john", { ["file.txt"] = "X" }, 1)
---```
---
---```lua
---  local ok = lfm.api.set_directory_tags("/home/john", nil) -- nil is explicit here
---```
---
---@param path string path to the directory
---@param tags table<string,string>|nil a map of filenames -> tag, `nil` clears and sets cols to `0`
---@param cols? integer number of columns (in characters) to print, if `nil`, leave as is
---@return boolean ok `true` on success, `false` if the directory wasn't loaded
function lfm.api.set_directory_tags(path, tags, cols) end

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

---@class Lfm.ModeDef
---@field name string The name of the mode.
---@field is_input? boolean true, if the mode takes input via the command line
---@field prefix? string The prefix, a string, shown in the command line.
---@field on_enter? function A function that is called when the mode is entered.
---@field on_return? function A function that is called when pressing enter while the mode is active.
---@field on_change? function A function that is called when the command line changes, e.g. keys are typed/deleted.
---@field on_esc? function A function that is called when pressing esc while the mode is active.
---@field on_exit? function A function that is called when the mode is exited.

---Callbacks can only be changed for lua modes, not builtin modes, where it will silently fail.
---@class Lfm.ModeUpdate
---@field prefix? string The prefix, a string, shown in the command line.
---@field on_enter? function|false Set `on_enter` callback. `false` removes
---@field on_return? function|false Set `on_enter` callback. `false` removes
---@field on_change? function|false Set `on_enter` callback. `false` removes
---@field on_esc? function|false Set `on_enter` callback. `false` removes
---@field on_exit? function|false Set `on_enter` callback. `false` removes

---
---Create a mode to lfm.
---
---Example:
---```lua
---  lfm.api.create_mode({
---    name = "my-mode",
---    input = true,
---    prefix = "My cool mode: ",
---    on_return = function()
--       local line = lfm.cmd.line_get()
---      lfm.api.mode("normal")
---      print("mode exited, command line was", line)
---    end,
---  })
---```
---
---Use the mode via
---```lua
---  lfm.api.mode("my-mode")
---```
---
---@param def Lfm.ModeDef
function lfm.api.create_mode(def) end

---
---Update an existing mode (currently only the prefix field).
---
---Example:
---```lua
---  lfm.api.update_mode("filter", {
---    prefix = "FILTER: ",
---  })
---```
---
---@param name string
---@param upd Lfm.ModeUpdate
function lfm.api.update_mode(name, upd) end

---
---Enter a mode.
---
---Example:
---```lua
---  lfm.api.mode("command")
---```
---
---@param name string The name of the mode.
function lfm.api.mode(name) end

---
---Get the current mode.
---
---Example:
---```lua
---  local mode = lfm.api.current_mode()
---  print("Current mode:", mode)
---```
---
---@return string mode
---@nodiscard
function lfm.api.current_mode() end

---
---Get the names of registered modes.
---
---Example:
---```lua
---  local modes = lfm.api.get_modes()
---  for _, mode in ipairs(modes) do
---    print(mode)
---  end
---```
---
---@return string[]
---@nodiscard
function lfm.api.get_modes() end

---@alias Lfm.Hook
---| '"on_start"'               # Lfm has started and read all configuration
---| '"on_exit"'                # Lfm is about to exit, called with exit status
---| '"on_chdir_pre"'           # Emitted before changing directories, called with PWD
---| '"on_chdir_post"'          # Emitted after changin directories, called with PWD
---| '"on_resize"'              # The window was resized
---| '"on_selection_change"'    # The selection changed
---| '"on_paste_buffer_change"' # The paste buffer changed
---| '"on_dir_loaded"'          # A new directory was loaded from disk, called with path
---| '"on_dir_updated"'         # A new directory was loaded from disk, called with path
---| '"on_mode_change"'         # Mode transition, called with mode name
---| '"on_focus_gained"'        # Terminal gained focus
---| '"on_focus_lost"'          # Terminal lost focus
---| '"on_exec_pre"'            # Before a foreground command is executed
---| '"on_exec_post"'           # After a foreground command is executed

---
---Register a function to hook into events. Returns an `id` with which the hook
---can be deregistered later. Curruntly supported hooks are
---
---Example:
---```lua
---  lfm.api.add_hook("on_dir_loaded", function(dir)
---    print(dir, "was loaded")
---  end)
---```
---
---@param name Lfm.Hook
---@param f function
---@return integer id
function lfm.api.add_hook(name, f) end

---
---Deregister the hook with the given `id` previously returned by `lfm.api.add_hook`.
---
---Example:
---```lua
---  local id = lfm.api.add_hook("on_start", function()
---    print("main loop started")
---  end)
---  lfm.api.del_hook(id)
---```
---
---@param id integer
function lfm.api.del_hook(id) end

---
---Move the cursor to a file in the current directory.
---
---Example:
---```lua
---  lfm.api.select("file.txt")
---```
---
---@param name string
function lfm.api.select(name) end
