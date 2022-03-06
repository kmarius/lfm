---@meta

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

---Set the command line. If three arguments are provided, the first argument
---sets the prefix. The cursor will be positioned between `left` and `right`.
---@param line string
---@overload fun(prefix: string, left: string, right: string)
function lfm.cmd.line_set(line) end

---Set the command line prefix.
---@param prefix string
function lfm.cmd.prefix_set(prefix) end
