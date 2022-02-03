---@meta

---@class lfmlib
lfm = {}

---Execute a command and redirect output/error to the UI.
---Supported options:
--- `opts.fork` should the command run in background (default: `false`)
--- `opts.out`  should stdout be captured, ignored with fork=false (default: `true`)
--- `opts.err`  should stderr be captured, ignored with fork=false (default: `true`)
---@param command string[]
---@param opts table
function lfm.execute(command, opts) end

---Set the timeout in milliseconds from now in which lfm will ignore keyboard input.
---@param duration integer in milliseconds.
function lfm.timeout(duration) end

---Find files the current directory. Moves the curser to to next file with the given prefix
---Returns true if only a single file in the current directory matches.
---@param prefix string
---@return boolean
function lfm.find(prefix) end

---Jumps to the next `lfm.find` match.
function lfm.find_next() end

---Jumps to the previous `lfm.find` match.
function lfm.find_prev() end

---Clear `lfm.find` matches.
function lfm.find_clear() end

---Search files in the current directory.
---@param string? string Omitting will remove highlighting.
function lfm.search(string) end

---Search files in the current directory, backwards.
---@param string? string Omitting will remove highlighting.
function lfm.search_back(string) end

---Go to the next search result.
---@param inclusive? boolean default: `false`
function lfm.search_next(inclusive) end

---Go to the previous search result.
---@param inclusive? boolean default: `false`
function lfm.search_prev(inclusive) end

---Disable highlights.
function lfm.nohighlight() end

---Show an error in the UI.
---@param msg string
function lfm.error(msg) end

---Show a message in the UI.
---@param msg string
function lfm.echo(msg) end

---@param keys string
function lfm.handle_key(keys) end

--Map a key sequence to a function in normal mode.
---@param seq string
---@param f function
---@param opts table Currently, only opts.desc is used for description
function lfm.map(seq, f, opts) end

--Map a key sequence to a function in command mode.
---@param seq string
---@param f function
---@param opts table Currently, only opts.desc is used for description
function lfm.cmap(seq, f, opts) end

--Crash lfm.
function lfm.crash() end

--Quit lfm.
function lfm.quit() end

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

---Clear the selection.
function lfm.fm.selection_clear() end

---Reverse selection of files in the current directory.
function lfm.fm.selection_reverse() end

---Toggle selection of the current file.
function lfm.fm.selection_toggle() end

---Add files to the current selection.
---@param files string[] table of strings.
function lfm.fm.selection_add(files) end

---Set the current selection.
---@param files string[] table of strings.
function lfm.fm.selection_set(files) end

---Get the current selection.
---@return string[] files table of files as strings.
function lfm.fm.selection_get() end

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

---@alias movemode
---| '"copy"'
---| '"move"'

---Get the current load and mode.
---@return movemode mode
---@return string[] files
function lfm.fm.load_get() end

---Set the current load and mode.
---@param mode movemode
---@param files string[]
function lfm.fm.load_set(mode, files) end

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

---Navigate to location given by dir
---@param dir string destination path
function lfm.fm.chdir(dir) end

---Clear the current load.
function lfm.fm.load_clear() end

lfm.log = {}

---@vararg any
function lfm.log.trace(...) end

---@vararg any
function lfm.log.debug(...) end

---@vararg any
function lfm.log.info(...) end

---@vararg any
function lfm.log.warn(...) end

---@vararg any
function lfm.log.error(...) end

---@vararg any
function lfm.log.fatal(...) end

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
function lfm.cmd.getline() end

---Get the current command line prefix.
---@return string prefix
function lfm.cmd.getprefix() end

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
function lfm.cmd.setline(line) end

---Set the command line prefix.
---@param prefix string
function lfm.cmd.setprefix(prefix) end

lfm.fn = {}

---Get the process id of the current instance.
---@return number PID
function lfm.fn.getpid() end

---Tokenize a string. For convenience, the first token is returned separately.
---@param str string
---@return string, string[]
function lfm.fn.tokenize(str) end

---Split a string into prefix, rest, where rest is the last space delimited token.
---Respects escaped spaces.
---@param str string
---@return string, string
function lfm.fn.split_last(str) end

---Escapes spaces in a string.
---@param str string
---@return string
function lfm.fn.quote_space(str) end

---Replaces "\\ " with " " in `str`.
---@param str string
---@return string
function lfm.fn.unquote_space(str) end

---Get the current working directory (usually with symlinks resolved)
---@return string
function lfm.fn.getcwd() end

---Get the present PWD, equivalent to `os.getenv("PWD")`.
---@return string
function lfm.fn.getpwd() end

---@alias Color string|integer

---@class ColorPair
---@field fg Color
---@field bg Color

---@class ExtColor
---@field ext string[]
---@field color ColorPair

---@class ColorLib
---@field patterns ExtColor[]
---@field copy ColorPair
---@field delete ColorPair
---@field dir ColorPair
---@field broken ColorPair
---@field exec ColorPair
---@field search ColorPair
---@field normal ColorPair
---@field current ColorPair

---@class configlib
---@field ratios integer[] assignable
---@field truncatechar string assignable, only the first character is used
---@field scrolloff integer assignable
---@field hidden boolean assignable
---@field preview boolean assignable
---@field previewer string assignable
---@field configpath string
---@field logpath string
---@field fifopath string
---@field dircache_size integer assignable
---@field previewcache_size integer assignable
---@field colors ColorLib
lfm.config = {}

return lfm
