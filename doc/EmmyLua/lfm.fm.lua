---@meta

lfm.fm = {}

---@class dir
---@field path string
---@field name string
---@field files table[string] table of filenames

---Set the filter string for the current directory.
---@param filter string The filter string.
function lfm.fm.filter(filter) end

---Get the filter string for the current directory.
---@return string filter The filter string.
function lfm.fm.getfilter() end

---Load a quickmark and navigate to the corrensponding directory.
---@param c string `char` of the mark. Currently only `'` supported.
function lfm.fm.mark_load(c) end

---Navigate into the directory at the current cursor position. If the current file
---is not a directory, its path is returned instead.
---@return string file
function lfm.fm.open() end

---Get the current directory.
---@return dir directory
function lfm.fm.current_dir() end

---Get the current file.
---@return string file
function lfm.fm.current_file() end

---Reverse selection of files in the current directory.
function lfm.fm.selection_reverse() end

---Toggle selection of the current file.
function lfm.fm.selection_toggle() end

---Add files to the current selection.
---@param files string[] table of strings.
function lfm.fm.selection_add(files) end

---Set the current selection. Empty table or nil clears.
---@param files string[] table of strings.
function lfm.fm.selection_set(files) end

---Get the current selection.
---@return string[] files table of files as strings.
function lfm.fm.selection_get() end

---Get the flatten level for the current directory.
---@return number
function lfm.fm.flatten_level() end

---Flatten `level` levels of the current directory.
---@param level number
function lfm.fm.flatten(level) end

---@alias sortoption
---| '"name"'
---| '"natural"'
---| '"ctime"'
---| '"size"'
---| '"random"'
---| '"dirfirst"'
---| '"nodirfirst"'
---| '"reverse"'
---| '"noreverse"'

---Set the sort method. Multiple options can be set at once. Later options may override previous ones.
---#Example:
---
---```
--- lfm.fm.sortby("ctime", "nodirfirst", "reverse")
---
---```
---@param opt1? sortoption
---@vararg sortoption
function lfm.fm.sortby(opt1, ...) end

---Start visual selection mode.
function lfm.fm.visual_start() end

---End visual selection mode.
function lfm.fm.visual_end() end
---Toggle visual selection mode.
function lfm.fm.visual_toggle() end

---Change directory to the parent of the current directory, unless in "/".
function lfm.fm.updir() end

---@alias pastemode
---| '"copy"'
---| '"move"'

---Get the current paste buffer and mode.
---@return string[] files
---@return pastemode mode
function lfm.fm.paste_buffer_get() end

---Set the current paste buffer and mode.
---@param files string[]
---@param mode? pastemode (default: "copy")
function lfm.fm.paste_buffer_set(files, mode) end

---Add the current selection to the load and change mode to MODE_MOVE.
function lfm.fm.cut() end

---Add the current selection to the load and change mode to MODE_COPY.
function lfm.fm.copy() end

---Check the current directory for changes and reload if necessary.
function lfm.fm.check() end

---Drop directory cache and reload visible directories from disk.
-- function lfm.fm.drop_cache() end

---Reload visible directories from disk.
function lfm.fm.reload() end

---Move the cursor to a file in the current directory.
---@param name string
function lfm.fm.sel(name) end

---Current height of the file manager, i.e. the maximum number shown of one directory.
---@return integer
function lfm.fm.get_height() end

---Move the cursor to the bottom.
function lfm.fm.bottom() end

---Move the cursor to the top.
function lfm.fm.top() end

---Move the cursor up.
---@param ct? number count, 1 if omitted
function lfm.fm.up(ct) end

---Move the cursor down.
---@param ct? number count, 1 if omitted
function lfm.fm.down(ct) end

---Scroll up while keeping the cursor on the current file (if possible).
function lfm.fm.scroll_up() end

---Scroll down while keeping the cursor on the current file (if possible).
function lfm.fm.scroll_down() end

---Navigate to location given by dir
---@param dir string destination path
function lfm.fm.chdir(dir) end
