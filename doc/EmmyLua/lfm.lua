---@meta

---@class Lfm.Mode
---@field name string Name of the mode.
---@field input boolean True, if the modes takes input form the command line.
---@field prefix string Prefix shown in the command line for an input mode.

---@class Lfm
---@field modes table<string, Lfm.Mode>
lfm = {}

---@alias Lfm.Version.BuildType
---| '"Debug"'
---| '"Release"'
---| '"RelWithDebInfo"'

---@class Lfm.Version
---@field info string version information string
---@field branch string branch
---@field commit string commit hash
---@field revcount number revision count
---@field build_type Lfm.Version.BuildType build type
lfm.version = {}

---Execute a foreground command.
---@param command string[]
function lfm.execute(command) end

---@class Lfm.SpawnOpts
---@field stdin? string|string[] Will be sent to the processe's stdin
---@field out? boolean|function Function to capture stdout
---@field err? boolean|function Function to capture stderr
---@field callback? function Function to capture the return value

---Spawn a background command. Returns the pid on success, nil otherwise.
---Supported options:
--- `opts.stdin`    string or a table of strings that will be sent to the processes stdin.
--- `opts.out`      should stdout be shown in the UI (default: `true`)
--- `opts.err`      should stderr be shown in the UI (default: `true`)
--- `opts.callback` called on exit with the command's return status
---
---`opts.out` and `opts.err` can instead be set to functions which will be called with
---each line output by the brogram. In this case, nothing is shown in the UI.
---@param command string[]
---@param opts? Lfm.SpawnOpts
---@return number? pid
function lfm.spawn(command, opts) end

---@alias Lfm.Hook
---| '"LfmEnter"'
---| '"ExitPre"'
---| '"ChdirPre"'
---| '"ChdirPost"'
---| '"SelectionChanged"'
---| '"Resized"'
---| '"PasteBufChange"'
---| '"DirLoaded"'
---| '"DirUpdated"'
---| '"ModeChanged"'
---| '"FocusGained"'
---| '"FocusLost"'

---Register a function to hook into events. Curruntly supported hooks are
---```
--- LfmEnter         Lfm has started and read all configuration
--- ExitPre          Lfm is about to exit
--- ChdirPre         Emitted before changing directories
--- ChdirPost        Emitted after changin directories
--- SelectionChanged The selection changed
--- Resized          The window was resized
--- PasteBufChange   The paste buffer changed
--- DirLoaded        A new directory was loaded from disk
--- DirUpdated       A new directory was loaded from disk
--- ModeChanged      Mode transition
--- FocusGained      Terminal gained focus
--- FocusLost        Terminal lost focus
---
---```
---@param name Lfm.Hook
---@param f function
function lfm.register_hook(name, f) end

---Schedule a lua function to run after `delay` milliseconds. Runs `f` immediately
---if `delay` non-positive.
---@param f function
---@param delay number
function lfm.schedule(f, delay) end

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
---@param ... any[]
function lfm.print(...) end

---If a message is shown in the statusline, clear it.
function lfm.message_clear() end

---@param keys string
function lfm.handle_key(keys) end

---@class Lfm.MapOpts
---@field desc? string Description of the mapping
---@field mode? string Name of the mode for a mode-only mapping

---@class Lfm.CMapOpts
---@field desc? string Description of the mapping

---Map a key sequence to a function in normal mode. The function is called with
---the command repetition count if it greater than 0 or nil if not.
---Unmap by passing nil instead of a function.
---@param seq string
---@param f? function
---@param opts? Lfm.MapOpts
function lfm.map(seq, f, opts) end

---Map a key sequence to a function in command mode. Unmap by passing nil instead
---of a function.
---@param seq string
---@param f? function
---@param opts? Lfm.CMapOpts
function lfm.cmap(seq, f, opts) end

---@class Lfm.Keymap
---@field desc string
---@field keys string
---@field f function

---Get a table of all maps for a mode. Pass the special mode "input" to get keys
---mapped via lfm.cmap.
---@param mode string mode
---@param prune boolean list reachable maps only
---@return Lfm.Keymap[]
function lfm.get_maps(mode, prune) end

---Clear all colors.
function lfm.colors_clear() end

---@class Lfm.ModeDef
---@field name string The name of the mode.
---@field input? boolean true, if the mode takes input via the command line
---@field prefix? string The prefix, a string, shown in the command line.
---@field on_enter? function A function that is called when the mode is entered.
---@field on_return? function A function that is called when pressing enter while the mode is active.
---@field on_change? function A function that is called when the command line changes, e.g. keys are typed/deleted.
---@field on_esc? function A function that is called when pressing esc while the mode is active.
---@field on_exit? function A function that is called when the mode is exited.

---Register a mode to lfm. A mode is given by a table t containing the following fields:
---```
--- t.name       -- The name of the mode.
--- t.input      -- true, if the mode takes input via the command line
--- t.prefix     -- The prefix, a string, shown in the command line.
--- t.on_enter   -- A function that is called when the mode is entered.
--- t.on_return  -- A function that is called when pressing enter while the mode is active.
--- t.on_change  -- A function that is called when the command line changes, e.g. keys are typed/deleted.
--- t.on_esc     -- A function that is called when pressing esc while the mode is active.
--- t.on_exit    -- A function that is called when the mode is exited.
---```
---@param t Lfm.ModeDef
function lfm.register_mode(t) end

---Enter a mode
---@param name string The name of the mode.
function lfm.mode(name) end

---Get the current mode.
---@return string The name of the current mode.
function lfm.current_mode() end

---Quit lfm.
---@param ret? number Exit status that will be reported by lfm.
function lfm.quit(ret) end

return lfm
