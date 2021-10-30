---@meta
lfm = {}

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

--Move the cursor to the bottom.
function lfm.fm.bottom() end

--Move the cursor to the top.
function lfm.fm.top() end

--Move the cursor up.
---@param ct? number count, 1 if omitted
function lfm.fm.up(ct) end

--Move the cursor down.
---@param ct? number count, 1 if omitted
function lfm.fm.down(ct) end

--Navigate to location given by dir
---@param dir string destination path
function lfm.fm.chdir(dir) end

--Clear the current load.
function lfm.fm.load_clear() end

lfm.log = {}

---@param msg string
function lfm.log.debug(msg) end

---@param msg string
function lfm.log.info(msg) end

---@param msg string
function lfm.log.trace(msg) end

return lfm
