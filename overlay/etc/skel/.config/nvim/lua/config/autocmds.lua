local au = vim.api.nvim_create_autocmd
local group = vim.api.nvim_create_augroup("lavender", { clear = true })

au("TextYankPost", { group = group, callback = function() vim.hl.on_yank({ timeout = 180 }) end })

-- вернуться туда, где курсор был в прошлый раз
au("BufReadPost", {
  group = group,
  callback = function(ev)
    local mark = vim.api.nvim_buf_get_mark(ev.buf, '"')
    local lines = vim.api.nvim_buf_line_count(ev.buf)
    if mark[1] > 0 and mark[1] <= lines and vim.bo[ev.buf].filetype ~= "gitcommit" then
      pcall(vim.api.nvim_win_set_cursor, 0, mark)
    end
  end,
})

-- перечитать файл, если он поменялся снаружи
au({ "FocusGained", "TermClose", "TermLeave" }, { group = group, command = "silent! checktime" })

-- служебные окна закрываются на q
au("FileType", {
  group = group,
  pattern = { "help", "qf", "man", "checkhealth", "lspinfo", "dap-float" },
  callback = function(ev)
    vim.bo[ev.buf].buflisted = false
    vim.keymap.set("n", "q", "<cmd>close<cr>", { buffer = ev.buf, silent = true })
  end,
})

-- отступы по языкам
au("FileType", {
  group = group,
  pattern = { "lua", "javascript", "typescript", "typescriptreact", "javascriptreact",
    "json", "jsonc", "html", "css", "scss", "yaml", "markdown", "kdl", "toml" },
  callback = function() vim.opt_local.shiftwidth = 2; vim.opt_local.tabstop = 2 end,
})
au("FileType", {
  group = group,
  pattern = { "markdown", "text", "gitcommit" },
  callback = function() vim.opt_local.wrap = true end,
})

-- создавать недостающие папки при сохранении
au("BufWritePre", {
  group = group,
  callback = function(ev)
    if ev.match:match("^%w%w+:[\\/][\\/]") then return end
    vim.fn.mkdir(vim.fn.fnamemodify(vim.uv.fs_realpath(ev.match) or ev.match, ":p:h"), "p")
  end,
})
