---@meta

---@class Lfm.Mode
---@field name string Name of the mode.
---@field input boolean True, if the modes takes input form the command line.
---@field prefix string Prefix shown in the command line for an input mode.

---@class Lfm.Dir
---@field path string
---@field name string
---@field files fun():string[] table of filenames
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
lfm.ui = require("lfm.ui")
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
---@field capture_stdout? true Capture stdout and return it in the `stdout` field of the execution result
---@field capture_stderr? true Capture stderr and return it in the `stderr` field of the execution result
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
---  local res, err = lfm.execute({ "fzf" }, { capture_stdout = true })
---  print(res.stdout[1])
---```
---
---Capture stderr.
---```lua
---  local res, err = lfm.execute({ "nvim", some_file }, { capture_stderr = true })
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
---@field on_stdout? fun(line: string)|true Function to capture stdout, or `true` to show output in the UI
---@field on_stderr? fun(line: string)|true Function to capture stderr, or `true` to show output in the UI
---@field env? table<string, string> Additional environment variables to set.
---@field on_exit? function Function to capture the return value
---@field dir? string Working directory for the process

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
---  local proc, err = lfm.spawn({ "cat" }, { on_stdin = true, on_stdout = true })
---  proc:write("hello") -- prints "hello" to the UI
---  proc:close()
---```
---
---@param line string
function proc:write(line) end

---
---Send a signal to the process.
---
---Example:
---```lua
---  local proc, err = lfm.spawn({ "sleep", "10" }, { on_exit = print })
---  -- ...
---  local ret = proc:send_signal(15)  -- 0, on success
---```
---
---@param sig integer signal number
---@return integer ret exit status of `kill`
function proc:send_signal(sig) end

---
---Close stdin. Also called, when the object is garbage collected. Safe to call multiple times.
---
---Example:
---```lua
---  local proc, err = lfm.spawn({ "cat" }, { on_stdin = true, on_stdout = true })
---  -- ...
---  -- use send and receive data to and from proc
---  -- ...
---  proc:close()
---```
---
function proc:close() end

---
---Spawn a background command. Returns the pid on success, `nil` otherwise.
---
---Example:
---```lua
---  lfm.spawn({"notify-send", "Hello", "from lfm"})
---```
---
---Capture exit status:
---```lua
---  local function on_exit(ret)
---    print("command exited with status " .. ret)
---  end
---  lfm.spawn({"true"}, { on_exit = on_exit })
---```
---
---Capture stdout:
---```lua
---  -- Prints directly to lfm:
---  lfm.spawn({"echo", "hello"}, { on_stdout = true })
---
---  -- Capture output with a function:
---  local function on_stdout(line)
---    print("received: " .. line)
---  end
---  lfm.spawn({"echo", "hello"}, { on_stdout = on_stdout })
---```
---
---Capture stderr:
---```lua
---  -- Prints nothing
---  lfm.spawn({"sh", "-c", "echo hello >&2"}, { on_stderr = true })
---
---  -- Capture stderr with a function:
---  local function on_stderr(line)
---    print("received: " .. line) .. " on stderr"
---  end
---  lfm.spawn({"echo", "hello"}, { on_stderr = on_stderr })
---```
---
---Sending input:
---```lua
---  -- Send stdin to command (which is then output by "cat" and sent to lfm)
---  lfm.spawn({"cat"}, { stdin = "Hello", on_stdout = true })
---
---  -- Or as a table
---  lfm.spawn({"cat"}, { stdin = { "Hello,", "Mike" }, on_stdout = true })
---
---  -- Or via a function
---  local proc, err = lfm.spawn({"cat"}, { stdin = true, on_stdout = true })
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
---which is passed to the callback function. On error, `on_exit` is called with `nil, errmsg`
---Currently, `lfm` is not accessible from seperate threads and only the modules luajit offers are loaded.
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
---An arguments can be passed to the thread via the third argument:
---```lua
---  local function adds_two(n)
---    return n + 2
---  end
---
---  lfm.thread(adds_two, print, 2) -- prints 4
---```
---
---@param chunk string|function Lua code to execute. Functions are attempted to be converted via string.dump
---@param on_exit? fun(res: any, err: string) Callback for the result/error
---@param arg? any An optional argument to pass to the thread
function lfm.thread(chunk, on_exit, arg) end

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

---
---Register a function to hook into events. Returns an `id` with which the hook
---can be deregistered later. Curruntly supported hooks are
---
---Example:
---```lua
---  lfm.register_hook("DirLoaded", function(dir)
---    print(dir, "was loaded")
---  end)
---```
---@param name Lfm.Hook
---@param f function
---@return integer id
function lfm.register_hook(name, f) end

---
---Deregister the hook with the given `id` previously returned by `lfm.register_hook`.
---
---Example:
---```lua
---  local id = lfm.register_hook("LfmEnter", function() end)
---  lfm.deregister_hook(id)
---```
---
---@param id integer
function lfm.deregister_hook(id) end

---
---Schedule a lua function to run after `delay` milliseconds. Runs `f` immediately after the programs main loop is back in control.
---if `delay` non-positive.
---
---Example:
---```lua
---  lfm.schedule(function()
---    print("Printed after 5 seconds")
---  end, 5000)
---```
---
---@param f function
---@param delay? integer delay in milliseconds
function lfm.schedule(f, delay) end

---
---Search files in the current directory.
---
---Example:
---```lua
---  lfm.search(".txt")
---```
---
---Clear search and highlight:
---```lua
---  lfm.search()
---```
---
---@param string? string Omitting will remove highlighting.
function lfm.search(string) end

---
---Search files in the current directory, backwards.
---
---Example:
---```lua
---  lfm.search_back(".txt")
---```
---
---Clear search and highlight:
---```lua
---  lfm.search_back()
---```
---
---@param string? string Omitting will remove highlighting.
function lfm.search_back(string) end

---
---Go to the next search result.
---
---Example:
---```lua
---  lfm.search_next()
---```
---```lua
---  lfm.search_next(true)
---```
---
---@param inclusive? boolean default: `false`
function lfm.search_next(inclusive) end

---
---Go to the previous search result.
---
---Example:
---```lua
---  lfm.search_prev()
---```
---```lua
---  lfm.search_prev(true)
---```
---
---@param inclusive? boolean default: `false`
function lfm.search_prev(inclusive) end

---
---Disable highlights.
---
---Example:
---```lua
---  lfm.nohighlight()
---```
---
function lfm.nohighlight() end

---@class Lfm.Print2Opts
---@field timeout? number timeout in milliseconds

---
---Display a message, optionally with timeout, after which it is cleared.
---
---Example:
---```lua
---  lfm.print2("Hello!", { timeout = 1000 })
---```
---
---@param msg string
---@param opts? Lfm.Print2Opts
function lfm.print2(msg, opts) end

---
---Show an error in the UI.
---
---Example:
---```lua
---  lfm.error("Oh, no!", "Something went wrong")
---```
---
---@param msg string
function lfm.error(msg) end

---
---If a message is shown in the statusline, clear it.
---
---Example:
---```lua
---  lfm.message_clear()
---```
---
function lfm.message_clear() end

---
---Clear all colors.
---
---Example:
---```lua
---    lfm.colors_clear()
---```
---
function lfm.colors_clear() end

---
---Quit lfm, optionally setting the exit status.
---
---Example:
---```lua
---  lfm.quit()
---```
---
---Set non-zero exit status:
---```lua
---  lfm.quit(1)
---```
---
---@param ret? integer Exit status that will be reported by lfm (default: `0`).
function lfm.quit(ret) end

return lfm
