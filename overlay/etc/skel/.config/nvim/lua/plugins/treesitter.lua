-- Treesitter: точная подсветка, отступы и сворачивание по синтаксису
local parsers = {
  "bash", "c", "cpp", "css", "diff", "gitcommit", "html", "java", "javascript", "json",
  "kdl", "lua", "luadoc", "make", "markdown", "markdown_inline", "python", "query",
  "regex", "rust", "toml", "tsx", "typescript", "vim", "vimdoc", "yaml",
}

return {
  {
    "nvim-treesitter/nvim-treesitter",
    branch = "main",
    lazy = false,
    build = ":TSUpdate",
    config = function()
      local ts = require("nvim-treesitter")
      ts.setup()
      -- парсеры собираются утилитой tree-sitter (ставится через Mason)
      if vim.fn.executable("tree-sitter") == 1 then
        local installed = ts.get_installed()
        local missing = vim.tbl_filter(function(p) return not vim.tbl_contains(installed, p) end, parsers)
        if #missing > 0 then ts.install(missing) end
      end
      vim.api.nvim_create_autocmd("FileType", {
        group = vim.api.nvim_create_augroup("lavender_ts", { clear = true }),
        callback = function(ev)
          if pcall(vim.treesitter.start, ev.buf) then
            vim.bo[ev.buf].indentexpr = "v:lua.require'nvim-treesitter'.indentexpr()"
          end
        end,
      })
    end,
  },
}
