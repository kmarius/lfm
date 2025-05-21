---@meta

lfm.fn = {}

---Get the process id of the current instance.
---```lua
---    local pid = lfm.fn.getpid()
---```
---@return integer pid
---@nodiscard
function lfm.fn.getpid() end

---Tokenize a string. For convenience, the first token is returned separately.
---```lua
---    local first, args = lfm.fn.tokenize("command arg1 arg2")
---    assert(first == "command")
---    assert(#args == 2)
---    assert(args[1] == "arg1" and args[2] == "args")
---```
---@param str string
---@return string, string[]
---@nodiscard
function lfm.fn.tokenize(str) end

---Fully normalize `path`. Replaces '~', '..', '.', duplicate //, trailing /. Returns an absolute path.
---```lua
---    local path = lfm.fn.normalize("~/.config/lfm")
---    local path = lfm.fn.normalize("../jane/.config/lfm")
---```
---@param path string
---@return string
---@nodiscard
function lfm.fn.normalize(path) end

---Split a string into prefix, rest, where rest is the last space delimited token.
---Respects escaped spaces. Useful for completion command line tokens. Doesn't strip white space
---```lua
---    local prefix, last = lfm.fn.split_last("command arg1 arg2")
---    assert(prefix == "command arg1")
---    assert(prefix == "arg2")
---```
---@param str string
---@return string, string
---@nodiscard
function lfm.fn.split_last(str) end

---Escapes spaces in a string.
---```lua
---    local str = lfm.fn.quote_space("str1 str2")
---    assert(str == "str1\\ str2")
---```
---@param str string
---@return string
---@nodiscard
function lfm.fn.quote_space(str) end

---Replaces `"\\ "` with `" "` in `str`.
---```lua
---    local str = lfm.fn.unquote_space("str1\\ str2")
---    assert(str == "str1 str2")
---```
---@param str string
---@return string
---@nodiscard
function lfm.fn.unquote_space(str) end

---Get the current working directory (usually with symlinks resolved)
---```lua
---    local cwd = lfm.fn.getcwd()
---```
---@return string
---@nodiscard
function lfm.fn.getcwd() end

---Get the present working directory, equivalent to `os.getenv("PWD")`.
---```lua
---    local pwd = lfm.fn.getpwd()
---```
---@return string
---@nodiscard
function lfm.fn.getpwd() end

---Get the mimetype of `file`. Returns `nil` on error.
---```lua
---    local mime = lfm.fn.mime("/home/john/file.txt")
---    if mime then
---      print(file, mime)
---    end
---```
---@param file string
---@return string?
---@nodiscard
function lfm.fn.mime(file) end
