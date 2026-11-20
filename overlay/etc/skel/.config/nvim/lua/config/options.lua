vim.g.mapleader = " "
vim.g.maplocalleader = "\\"
vim.g.loaded_netrw = 1 -- вместо netrw — neo-tree
vim.g.loaded_netrwPlugin = 1

-- инструменты, поставленные через Mason, доступны сразу
vim.env.PATH = vim.fn.stdpath("data") .. "/mason/bin:" .. vim.env.PATH

local o = vim.opt
o.number = true
o.relativenumber = true
o.cursorline = true
o.signcolumn = "yes"
o.mouse = "a"
o.clipboard = "unnamedplus" -- общий буфер обмена с системой (wl-copy)
o.expandtab = true
o.shiftwidth = 4
o.tabstop = 4
o.smartindent = true
o.ignorecase = true
o.smartcase = true
o.termguicolors = true
o.scrolloff = 6
o.sidescrolloff = 8
o.splitright = true
o.splitbelow = true
o.undofile = true -- история отмен сохраняется между запусками
o.swapfile = false
o.updatetime = 250
o.timeoutlen = 400
o.wrap = false
o.linebreak = true
o.list = true
o.listchars = { tab = "→ ", trail = "·", nbsp = "␣" }
o.fillchars = { eob = " ", fold = " ", foldopen = "\u{f078}", foldclose = "\u{f054}", foldsep = " " }
o.showmode = false
o.laststatus = 3
o.pumheight = 12
o.confirm = true
o.winborder = "rounded"
o.smoothscroll = true
o.foldlevel = 99
o.foldlevelstart = 99
o.foldmethod = "expr"
o.foldexpr = "v:lua.vim.treesitter.foldexpr()"
o.foldtext = ""
o.spelllang = { "ru", "en" }

-- команды работают и в русской раскладке
o.langmap = "ФИСВУАПРШОЛДЬТЩЗЙКЫЕГМЦЧНЯ;ABCDEFGHIJKLMNOPQRSTUVWXYZ,"
  .. "фисвуапршолдьтщзйкыегмцчня;abcdefghijklmnopqrstuvwxyz"

vim.diagnostic.config({
  severity_sort = true,
  underline = true,
  update_in_insert = false,
  virtual_text = { prefix = "●", spacing = 2, source = "if_many" },
  float = { border = "rounded", source = true },
  signs = {
    text = {
      [vim.diagnostic.severity.ERROR] = "\u{f057} ",
      [vim.diagnostic.severity.WARN] = "\u{f071} ",
      [vim.diagnostic.severity.INFO] = "\u{f05a} ",
      [vim.diagnostic.severity.HINT] = "󰌵 ",
    },
  },
})
