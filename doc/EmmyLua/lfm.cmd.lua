---@meta

---@class Lfm.Cmd
---Utilities for manipulating the built-in command line.
lfm.cmd = {}

---Clear the command line.
---```lua
---    lfm.cmd.clear()
---```
function lfm.cmd.clear() end

---Delete the character to the left.
---```lua
---    lfm.cmd.delete()
---```
function lfm.cmd.delete() end

---Delete the character to the right.
---```lua
---    lfm.cmd.delete_right()
---```
function lfm.cmd.delete_right() end

---Delete the word to the right.
---```lua
---    lfm.cmd.delete_word()
---```
function lfm.cmd.delete_word() end

---Delete to the beginning of the line.
---```lua
---    lfm.cmd.delete_line_left()
---```
function lfm.cmd.delete_line_left() end

---Move cursor one word left.
---```lua
---    lfm.cmd.word_left()
---```
function lfm.cmd.word_left() end

---Move cursor one word right.
---```lua
---    lfm.cmd.word_right()
---```
function lfm.cmd.word_right() end

---Move cursor to the end.
---```lua
---    lfm.cmd._end()
---```
function lfm.cmd._end() end

---Move cursor to the beginning of the line.
---```lua
---    lfm.cmd.home()
---```
function lfm.cmd.home() end

---Insert a character at the current cursor position.
---```lua
---    lfm.cmd.insert("ö")
---```
---@param c string The character
function lfm.cmd.insert(c) end

---Move the cursor to the left.
---```lua
---    lfm.cmd.left()
---```
function lfm.cmd.left() end

---Move the cursor to the right.
---```lua
---    lfm.cmd.right()
---```
function lfm.cmd.right() end

---Get the current command line string.
---```lua
---    local line = lfm.cmd.line_get()
---    -- do something with line
---```
---@return string
---@nodiscard
function lfm.cmd.line_get() end

---Set the command line. If two arguments are provided.
---The cursor will be positioned between `left` and `right`.
---```lua
---    -- sets the command line to file.txt with the cursor at the end
---    lfm.cmd.line_set("file.txt")
---
---    -- sets the cursor before the dot
---    lfm.cmd.line_set("file", ".txt")
---
---    -- clears:
---    lfm.cmd.line_set()
---```
---@param left? string
---@param right? string
function lfm.cmd.line_set(left, right) end

---Get the current command line prefix.
---```lua
---    local prefix = lfm.cmd.prefix_get()
---    -- do something with prefix
---```
---@return string prefix
---@nodiscard
function lfm.cmd.prefix_get() end

---Toggle between insert and overwrite mode.
---```lua
---    lfm.cmd.toggle_overwrite()
---```
function lfm.cmd.toggle_overwrite() end

---Append a line to history.
---```lua
---    lfm.cmd.history_append(':', "cd /tmp")
---```
---@param prefix string
---@param line string
function lfm.cmd.history_append(prefix, line) end

---Get the next line from history.
---```lua
---    local line = lfm.cmd.history_next()
---    -- do something with line
---```
---@return string
---@nodiscard
function lfm.cmd.history_next() end

---Get the previous line from history.
---```lua
---    local line = lfm.cmd.history_prev()
---    -- do something with line
---```
---@return string
---@nodiscard
function lfm.cmd.history_prev() end
