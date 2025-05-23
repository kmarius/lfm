---@meta

---@class Lfm.Mode
---@field name string Name of the mode.
---@field input boolean True, if the modes takes input form the command line.
---@field prefix string Prefix shown in the command line for an input mode.

---@class Lfm.Dir
---@field path string
---@field name string
---@field files table[string] table of filenames
---@field sortopts Lfm.SortOpts

---@alias Lfm.SortType
---| '"name"'
---| '"natural"'
---| '"ctime"'
---| '"mtime"'
---| '"atime"'
---| '"size"'
---| '"random"'

---@class Lfm.SortOpts
---@field type? Lfm.SortType
---@field dirfirst? boolean
---@field reverse? boolean

---@alias Lfm.PasteMode
---| '"copy"'
---| '"move"'

---@alias Lfm.Info
---| '"size"'
---| '"atime"'
---| '"ctime"'
---| '"mtime"'

---@alias Lfm.FilterType
---| '"filter"'
---| '"fuzzy"'
---| '"lua"'

---@alias Lfm.FilterFunction fun(name: string):any

---@class Lfm
---@field modes table<string, Lfm.Mode>
lfm = {}

-- Preloaded modules:

lfm.compl = require("lfm.compl")
lfm.fs = require("lfm.fs")
lfm.functions = require("lfm.functions")
lfm.inspect = require("lfm.inspect")
lfm.jumplist = require("lfm.jumplist")
lfm.mode = require("lfm.mode")
lfm.quickmarks = require("lfm.quickmarks")
lfm.rifle = require("lfm.rifle")
lfm.search = require("lfm.search")
lfm.shell = require("lfm.shell")
lfm.trash = require("lfm.trash")
lfm.util = require("lfm.util")

---@alias Lfm.Version.BuildType
---| '"Debug"'
---| '"Release"'
---| '"RelWithDebInfo"'

---@class (exact) Lfm.Version
---@field info string version information string
---@field branch string branch
---@field commit string commit hash
---@field revcount integer revision count
---@field build_type Lfm.Version.BuildType build type
lfm.version = {}

-- TODO: make it possible to print stdout/stderr to the ui, e.g. by passing print to stdout/err

---@class Lfm.ExecuteOpts
---@field stdin? string|string[] Send data to stdin
---@field stdout? true Capture stdout and return it in the `stdout` field of the execution result
---@field stderr? true Capture stderr and return it in the `stderr` field of the execution result
---@field env? table<string, string> Additional environment variables to set.

---@class Lfm.ExecuteResult
---@field status integer exit status
---@field stdout? string[] standard output, if requested
---@field stderr? string[] standard error, if requested

---
---Execute a foreground command. The hooks `ExecPre` and `ExecPost` are run
---before and after a command executes.
---
---Example:
---```lua
---  local res, err = lfm.execute({ "nvim", some_file })
---  if not res then
---    print(err)
---  else
---    print("status:", res.status)
---  end
---```
---
---Capture stdout. Really only works if supported by the executable.
---```lua
---  local res, err = lfm.execute({ "fzf" }, { stdout = true })
---  print(res.stdout[1])
---```
---
---Capture stderr.
---```lua
---  local res, err = lfm.execute({ "nvim", some_file }, { stderr = true })
---  print(res.stderr[1])
---```
---
---Send stdin.
---```lua
---  local res, err = lfm.execute({ "less" }, { stdin = { "line1", "line2" } })
---```
---
---Modify environment:
---```lua
---  local res, err = lfm.execute({ cmd, args }, { env = { SOME_VAR = "value" } })
---```
---
---@param command string[]
---@param opts? Lfm.ExecuteOpts
---@return Lfm.ExecuteResult result
---@return string? error
function lfm.execute(command, opts) end

---@class Lfm.SpawnOpts
---@field stdin? string|string[]|true Will be sent to the process' stdin. If `true`, input can be sent to the process via `proc:write`.
---@field stdout? fun(line: string)|true Function to capture stdout, or `true` to show output in the UI
---@field stderr? fun(line: string)|true Function to capture stderr, or `true` to show output in the UI
---@field env? table<string, string> Additional environment variables to set.
---@field callback? function Function to capture the return value

---
---Process handle.
---
---@class Lfm.SpawnProc
---@field pid integer pid
local proc = {}

---
---Send data to stdin. No newlines are added. Throws an error if stdin is closed, or `write` fails.
---In particular, `lfm.spawn` must be called with `opts.stdin = true`.
---
---Example:
---```lua
---  local proc, err = lfm.spawn({ "cat" }, { stdin = true, stdout = true })
---  proc:write("hello") -- prints "hello" to the UI
---  proc:close()
---```
---
---@param line string
function proc:write(line) end

---
---Close stdin. Also called, when the object is garbage collected. Safe to call multiple times.
---
---Example:
---```lua
---  local proc, err = lfm.spawn({ "cat" }, { stdin = true, stdout = true })
---  -- ...
---  -- use send and receive data to and from proc
---  -- ...
---  proc:close()
---```
---
function proc:close() end

---
---Spawn a background command. Returns the pid on success, nil otherwise.
---
---Example:
---```lua
---  lfm.spawn({"notify-send", "Hello", "from lfm"})
---```
---
---Capture exit status:
---```lua
---  local function callback(ret)
---    print("command exited with status " .. ret)
---  end
---  lfm.spawn({"true"}, { callback = callback })
---```
---
---Capture stdout:
---```lua
---  -- Prints directly to lfm:
---  lfm.spawn({"echo", "hello"}, { stdout = true })
---
---  -- Capture output with a function:
---  local function callback(line)
---    print("received: " .. line)
---  end
---  lfm.spawn({"echo", "hello"}, { stdout = callback })
---```
---
---Capture stderr:
---```lua
---  -- Prints nothing
---  lfm.spawn({"sh", "-c", "echo hello >&2"}, { stderr = true })
---
---  -- Capture stderr with a function:
---  local function callback(line)
---    print("received: " .. line) .. " on stderr"
---  end
---  lfm.spawn({"echo", "hello"}, { err = callback })
---```
---
---Sending input:
---```lua
---  -- Send stdin to command (which is then output by "cat" and sent to lfm)
---  lfm.spawn({"cat"}, { stdin = "Hello", stdout = true })
---
---  -- Or as a table
---  lfm.spawn({"cat"}, { stdin = { "Hello,", "Mike" }, stdout = true })
---
---  -- Or via a function
---  local proc, err = lfm.spawn({"cat"}, { stdin = true, stdout = true })
---  proc:write("Hello")
---  proc:close() -- close stdin so that cat exits
---````
---
---Modify environment:
---```lua
---  local proc, err = lfm.spawn({ cmd, args }, { env = { SOME_VAR = "value" } })
---```
---
---@param command string[]
---@param opts? Lfm.SpawnOpts
---@return Lfm.SpawnProc? proc
---@return string? error
function lfm.spawn(command, opts) end

---
---Execute a chunk of lua code in a seperate thread. The chunk may return up one return value,
---which is serialized with msgpack and passed to the callback function. On error, callback is called with nil, errmsg
---This functions throws if the "mpack" library can not be loaded. Long running code will currently block exiting lua indefinitely.
---
---Example:
---```lua
---  lfm.thread([[
---    return 2 + 2
---  ]], function(val, err)
---    if err then
---      error(err)
---    end
---    print(val)
---  end)
---```
---Functions can be converted into bytecode using `string.dump` and passed to `lfm.thread`. Note that any upvalue
---will be `nil` when executed.
---
---Example:
---```lua
---  local function test()
---    return 2 + 2
---  end
---
---  local bc = string.dump(test)
---
---  lfm.thread(bc, print)
---```
---
---@param chunk string Lua code to execute
---@param callback? fun(res: any, err: string) Callback for the result/error
function lfm.thread(chunk, callback) end

---@alias Lfm.Hook
---| '"LfmEnter"'         # Lfm has started and read all configuration
---| '"ExitPre"'          # Lfm is about to exit, called with exit status
---| '"ChdirPre"'         # Emitted before changing directories, called with PWD
---| '"ChdirPost"'        # Emitted after changin directories, called with PWD
---| '"SelectionChanged"' # The selection changed
---| '"Resized"'          # The window was resized
---| '"PasteBufChange"'   # The paste buffer changed
---| '"DirLoaded"'        # A new directory was loaded from disk, called with path
---| '"DirUpdated"'       # A new directory was loaded from disk, called with path
---| '"ModeChanged"'      # Mode transition, called with mode name
---| '"FocusGained"'      # Terminal gained focus
---| '"FocusLost"'        # Terminal lost focus
---| '"ExecPre"'          # Before a foreground command is executed
---| '"ExecPost"'         # After a foreground command is executed

---Register a function to hook into events. Returns an `id` with which the hook
---can be deregistered later. Curruntly supported hooks are
---```lua
---    lfm.register_hook("DirLoaded", function(dir)
---      print(dir, "was loaded")
---    end)
---```
---@param name Lfm.Hook
---@param f function
---@return integer id
function lfm.register_hook(name, f) end

---Deregister the hook with the given `id` previously returned by `lfm.register_hook`.
---```lua
---    local id = lfm.register_hook("LfmEnter", function() end)
---    lfm.deregister_hook(id)
---```
---@param id integer
function lfm.deregister_hook(id) end

---Schedule a lua function to run after `delay` milliseconds. Runs `f` immediately after the programs main loop is back in control.
---if `delay` non-positive.
---```lua
---    lfm.schedule(function()
---      print("Printed after 5 seconds")
---    end, 5000)
---```
---@param f function
---@param delay? integer delay in milliseconds
function lfm.schedule(f, delay) end

---Search files in the current directory.
---```lua
---    lfm.search(".txt")
---```
---Clear search and highlight:
---```lua
---    lfm.search()
---```
---@param string? string Omitting will remove highlighting.
function lfm.search(string) end

---Search files in the current directory, backwards.
---```lua
---    lfm.search_back(".txt")
---```
---Clear search and highlight:
---```lua
---    lfm.search_back()
---```
---@param string? string Omitting will remove highlighting.
function lfm.search_back(string) end

---Go to the next search result.
---```lua
---    lfm.search_next()
---```
---```lua
---    lfm.search_next(true)
---```
---@param inclusive? boolean default: `false`
function lfm.search_next(inclusive) end

---Go to the previous search result.
---```lua
---    lfm.search_prev()
---```
---```lua
---    lfm.search_prev(true)
---```
---@param inclusive? boolean default: `false`
function lfm.search_prev(inclusive) end

---Disable highlights.
---```lua
---    lfm.nohighlight()
---```
function lfm.nohighlight() end

---Show an error in the UI.
---```lua
---    lfm.error("Oh, no!", "Something went wrong")
---```
---@param msg string
function lfm.error(msg) end

---If a message is shown in the statusline, clear it.
---```lua
---    lfm.message_clear()
---```
function lfm.message_clear() end

---Handle a key sequence.
---```lua
---    -- select the first three files
---    lfm.handle_key("ggV2jV")
---
---    -- Type a command into the command line and execute it
---    lfm.handle_key(":cd ~<Enter>")
---```
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
---### Basic usage:
---```lua
---    lfm.map("P", function()
---      print("hey")
---    end, { desc = "My cool mapping" })
---```
---```lua
---    -- unmap "P"
---    lfm.map("P", nil)
---```
---### Mode mappings:
---```lua
---    -- Only active "my-mode"
---    lfm.map("P", function()
---      print("hey")
---    end, { desc = "My cool mapping", mode = "my-mode" })
---```
---### Accepting a command count:
---```lua
---    -- typing 2P prints "count: 2"
---    lfm.map("P", function(ct)
---       if ct then
---         print("count: " .. ct)
---       else
---         print("no count given")
---       end
---    end, { desc = "My cool mapping" })
---```
---@param seq string
---@param f? function
---@param opts? Lfm.MapOpts
function lfm.map(seq, f, opts) end

---Map a key sequence to a function in command mode. Unmap by passing nil instead
---of a function.
---```lua
---    lfm.cmap("<c-d>", function()
---      lfm.handle_key(os.date("%Y-%M-%d"))
---    end, { desc = "Insert the current date" })
---
---    -- unmap:
---    lfm.cmap("<c-d>", nil)
---```
---@param seq string
---@param f? function
---@param opts? Lfm.CMapOpts
function lfm.cmap(seq, f, opts) end

---@class Lfm.Keymap
---@field desc string
---@field keys string
---@field f function

---Get a table of all maps for a mode. Pass the special mode `"input"` to get keys
---mapped via `lfm.cmap`. If the `prune` parameter is set, only reachable maps are
---returned, i.e. if both `"g"` and `"gn"` are mapped, `"gn"` is not reachable and
---therefore not included.
---```lua
---    local maps = lfm.get_maps("normal")
---    for _, map in pairs(maps) do
---      print(map.keys, map.desc)
---      local f = map.f -- holds the function
---    end
---```
---@param mode string
---@param prune? boolean list reachable maps only (default: `false`)
---@return Lfm.Keymap[]
---@nodiscard
function lfm.get_maps(mode, prune) end

---Clear all colors.
---```lua
---    lfm.colors_clear()
---```
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
---```lua
---    lfm.register_mode({
---      name = "my-mode",
---      input = true,
---      prefix = "My cool mode: ",
---      on_return = function()
--         local line = lfm.cmd.line_get()
---        lfm.mode("normal")
---        print("mode exited, command line was", line)
---      end,
---    })
---```
---Use the mode via
---```lua
---    lfm.mode("my-mode")
---```
---@param def Lfm.ModeDef
function lfm.register_mode(def) end

---Enter a mode.
---```lua
---    lfm.mode("command")
---```
---@param name string The name of the mode.
function lfm.mode(name) end

---Get the current mode.
---```lua
---    local mode = lfm.current_mode()
---    print("Current mode:", mode)
---```
---@return string mode
---@nodiscard
function lfm.current_mode() end

---Get the names of registered modes.
---```lua
---    local modes = lfm.get_modes()
---    for _, mode in ipairs(modes) do
---      print(mode)
---    end
---```
---@return string[]
---@nodiscard
function lfm.get_modes() end

---Quit lfm, optionally setting the exit status.
---```lua
---    lfm.quit()
---```
---Set non-zero exit status:
---```lua
---    lfm.quit(1)
---```
---@param ret? integer Exit status that will be reported by lfm (default: `0`).
function lfm.quit(ret) end

return lfm
