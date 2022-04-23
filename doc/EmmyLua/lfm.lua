---@meta

---@class lfmlib
lfm = {}

---Evaluate `expr` as if typed into the command line.
---@param expr string
function lfm.eval(expr) end

---Execute a foreground command.
---@param command string[]
function lfm.execute(command) end

---Spawn a background command. Returns the pid on success, nil otherwise.
---Supported options:
--- `opts.stdin` string or a table of strings that will be sent to the processes stdin.
--- `opts.out`   should stdout be shown in the UI (default: `true`)
--- `opts.err`   should stderr be shown in the UI (default: `true`)
---
---`opts.out` and `opts.err` can instead be set to functions which will be called with
---each line output by the brogram. In this case, nothing is shown in the UI.
---@param command string[]
---@param opts? table
---@return number? pid
function lfm.spawn(command, opts) end

---Set the timeout in milliseconds from now in which lfm will ignore keyboard input.
---@param duration integer in milliseconds.
function lfm.timeout(duration) end

---Schedule a lua function to run after `delay` milliseconds. Runs `f` immediately
---if `delay` non-positive.
---@param f function
---@param delay number
function lfm.schedule(f, delay) end

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

---If a message is shown in the statusline, clear it.
function lfm.message_clear() end

---@param keys string
function lfm.handle_key(keys) end

---Map a key sequence to a function in normal mode. Unmap by passing nil instead
---of a function.
---@param seq string
---@param f function
---@param opts table Currently, only opts.desc is used for description
function lfm.map(seq, f, opts) end

---Map a key sequence to a function in command mode. Unmap by passing nil instead
---of a function.
---@param seq string
---@param f function
---@param opts table Currently, only opts.desc is used for description
function lfm.cmap(seq, f, opts) end

---@class keymap
---@field desc string
---@field keys string
---@field f function

---Get a table of all maps.
---@param prune? boolean list reachable maps only (default: true)
---@return keymap[]
function lfm.get_maps(prune) end

---Get a table of all cmaps.
---@param prune? boolean list reachable maps only (default: true)
---@return keymap[]
function lfm.get_cmaps(prune) end

---Clear all colors.
function lfm.colors_clear() end

--Crash lfm.
function lfm.crash() end

--Quit lfm.
function lfm.quit() end

return lfm
