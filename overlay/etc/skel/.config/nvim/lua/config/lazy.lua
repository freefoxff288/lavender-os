local path = vim.fn.stdpath("data") .. "/lazy/lazy.nvim"
if not vim.uv.fs_stat(path) then
  local out = vim.fn.system({ "git", "clone", "--filter=blob:none", "--branch=stable",
    "https://github.com/folke/lazy.nvim.git", path })
  if vim.v.shell_error ~= 0 then
    vim.api.nvim_echo({ { "Не удалось скачать lazy.nvim:\n" .. out, "ErrorMsg" } }, true, {})
    return
  end
end
vim.opt.rtp:prepend(path)

require("lazy").setup({
  spec = { { import = "plugins" } },
  install = { colorscheme = { "lavender" } },
  rocks = { enabled = false },
  checker = { enabled = true, notify = false }, -- тихо проверять обновления
  change_detection = { notify = false },
  ui = { border = "rounded", title = " плагины " },
  performance = {
    rtp = { disabled_plugins = { "gzip", "tarPlugin", "tohtml", "tutor", "zipPlugin" } },
  },
})

vim.cmd.colorscheme("lavender")
