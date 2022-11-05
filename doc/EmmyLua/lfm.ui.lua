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

---Draws a menu on screen.
---```
--
--- lfm.ui.menu() -- hide menu
--- lfm.ui.menu({"line1", "line2"})
--- lfm.ui.menu("line1\nline2")
---
---```
---@param menu string[]|string
function lfm.ui.menu(menu) end

---Show all previously shown errors and messages.
---@return string[] messages
function lfm.ui.messages() end

---Con notcurses open images?
---@return boolean
function lfm.ui.notcurses_canopen_images() end

---Con notcurses draw images with halfblocks?
---@return boolean
function lfm.ui.notcurses_canhalfblock() end

---Con notcurses draw images with quadrants?
---@return boolean
function lfm.ui.notcurses_canquadrant() end

---Con notcurses draw images with sextants?
---@return boolean
function lfm.ui.notcurses_cansextant() end

---Con notcurses draw images with braille?
---@return boolean
function lfm.ui.notcurses_canbraille() end

---Con notcurses draw pixel perfect bitmaps?
---@return boolean
function lfm.ui.notcurses_canpixel() end
