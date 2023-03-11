---@meta

lfm.fn = {}

---Get the process id of the current instance.
---@return number PID
function lfm.fn.getpid() end

---Tokenize a string. For convenience, the first token is returned separately.
---@param str string
---@return string, string[]
function lfm.fn.tokenize(str) end

---Fully qualify `path`. Replaces '~', '..', '.', returns an absolute path.
---@param path string
---@return string
function lfm.fn.qualify(path) end

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

---Get the mimetype of `file`. Returns nil on error.
---@param file string
---@return string
function lfm.fn.mime(file) end
