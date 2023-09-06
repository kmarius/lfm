---@meta

---@class Lfm.Cmd
---Utilities for manipulating the built-in command line.
lfm.cmd = {}

---Clear the command line.
function lfm.cmd.clear() end

---Delete the character to the left.
function lfm.cmd.delete() end

---Delete the character to the right.
function lfm.cmd.delete_right() end

---Delete the word to the right.
function lfm.cmd.delete_word() end

---Delete to the beginning of the line.
function lfm.cmd.delete_line_left() end

---Move cursor one word left.
function lfm.cmd.word_left() end

---Move cursor one word right.
function lfm.cmd.word_right() end

---Move cursor to the end.
function lfm.cmd._end() end

---Get the current command line string.
---@return string
function lfm.cmd.line_get() end

---Get the current command line prefix.
---@return string prefix
function lfm.cmd.prefix_get() end

---Move cursor to the beginning.
function lfm.cmd.home() end

---Insert a character at the current cursor position.
---@param c string
function lfm.cmd.insert(c) end

---Move the cursor to the left.
function lfm.cmd.left() end

---Move the cursor to the right.
function lfm.cmd.right() end

---Set the command line. If two arguments are provided.
---The cursor will be positioned between `left` and `right`.
---@param left string
---@param right? string
function lfm.cmd.line_set(left, right) end

---Toggle between insert and overwrite mode.
function lfm.cmd.toggle_overwrite() end

---Append a line to history.
---@param prefix string
---@param line string
function lfm.cmd.history_append(prefix, line) end

---Get the next line from history.
---@return string
function lfm.cmd.history_next() end

---Get the previous line from history.
---@return string
function lfm.cmd.history_prev() end
