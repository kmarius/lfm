---@meta

lfm.ui = {}

---Get the height in characters of the UI.
---```lua
---    local height = lfm.ui.get_height()
---    print("height:", height)
---```
---@return integer height
---@nodiscard
function lfm.ui.get_height() end

---Get the width in characters of the UI.
---```lua
---    local width = lfm.ui.get_width()
---    print("width:", width)
---```
---@return integer width
---@nodiscard
function lfm.ui.get_width() end

---Clear the UI and redraw.
---```lua
---    lfm.ui.clear()
---```
function lfm.ui.clear() end

---Request redraw. If no `force` is set only parts the need redrawing are drawn.
---Drawing happens _after_ execution of the current lua code finishes and the main
---event loop idles.
---```lua
---    lfm.ui.redraw()
---```
---Redraw everything:
---```lua
---    lfm.ui.redraw(true)
---```
---@param force? boolean use force (default: `false`)
function lfm.ui.redraw(force) end

---Draws a menu on screen.
---```lua
---    lfm.ui.menu() -- hide menu
---    lfm.ui.menu({"line1", "line2"})
---    lfm.ui.menu("line1\nline2")
---```
---@param menu nil|string[]|string
function lfm.ui.menu(menu) end

---Show all previously shown errors and messages.
---```lua
---    local messages = lfm.ui.messages()
---    for i, message in ipairs(messages) do
---      print(i, message)
---    end
---```
---@return string[] messages
---@nodiscard
function lfm.ui.messages() end

---Con notcurses open images?
---```lua
---    local can = lfm.ui.notcurses_canopen_images()
---```
---@return boolean
---@nodiscard
function lfm.ui.notcurses_canopen_images() end

---Con notcurses draw images with halfblocks?
---```lua
---    local can = lfm.ui.notcurses_canhalfblock()
---```
---@return boolean
---@nodiscard
function lfm.ui.notcurses_canhalfblock() end

---Con notcurses draw images with quadrants?
---```lua
---    local can = lfm.ui.notcurses_canquadrant()
---```
---@return boolean
---@nodiscard
function lfm.ui.notcurses_canquadrant() end

---Con notcurses draw images with sextants?
---```lua
---    local can = lfm.ui.notcurses_cansextant()
---```
---@return boolean
---@nodiscard
function lfm.ui.notcurses_cansextant() end

---Con notcurses draw images with braille?
---```lua
---    local can = lfm.ui.notcurses_canbraille()
---```
---@return boolean
---@nodiscard
function lfm.ui.notcurses_canbraille() end

---Con notcurses draw pixel perfect bitmaps?
---```lua
---    local can = lfm.ui.notcurses_canpixel()
---```
---@return boolean
---@nodiscard
function lfm.ui.notcurses_canpixel() end
