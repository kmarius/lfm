---@meta

lfm.ui = {}

---Height of the UI.
function lfm.ui.get_height() end

---Width of the UI.
function lfm.ui.get_width() end

---Clear the UI and redraw.
function lfm.ui.clear() end

---Request redraw.
function lfm.ui.draw() end

---Append a line to history.
---@param line string
function lfm.ui.history_append(line) end

---Get the next line from history.
---@return string
function lfm.ui.history_next() end

---Get the previous line from history.
---@return string
function lfm.ui.history_prev() end

---Draws a menu on screen.
---```
--
--- lfm.ui.menu() -- hide menu
--- lfm.ui.menu({"line1", "line2"})
---
---```
---@param menu string[]
function lfm.ui.menu(menu) end

---Show all previously shown errors and messages.
---@return string[] messages
function lfm.ui.messages() end
