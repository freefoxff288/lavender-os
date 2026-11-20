-- Языковые серверы: автодополнение, переходы, ошибки, переименование
local servers = { -- имена пакетов Mason
  "lua-language-server", "pyright", "ruff", "typescript-language-server",
  "bash-language-server", "clangd", "html-lsp", "css-lsp", "json-lsp",
  "marksman", "taplo",
}
local tools = {
  "tree-sitter-cli", "stylua", "prettier", "shfmt", "clang-format", "debugpy", "codelldb",
}

return {
  {
    "mason-org/mason.nvim",
    cmd = "Mason",
    opts = { ui = { border = "rounded", icons = { package_installed = "●", package_pending = "◐", package_uninstalled = "○" } } },
  },
  {
    "WhoIsSethDaniel/mason-tool-installer.nvim",
    event = "VeryLazy",
    dependencies = { "mason-org/mason.nvim" },
    opts = { ensure_installed = vim.list_extend(vim.deepcopy(servers), tools), run_on_start = true },
  },
  { "folke/lazydev.nvim", ft = "lua", opts = { library = { { path = "${3rd}/luv/library", words = { "vim%.uv" } }, "snacks.nvim" } } },
  {
    "neovim/nvim-lspconfig",
    event = { "BufReadPre", "BufNewFile" },
    dependencies = { "mason-org/mason.nvim", "mason-org/mason-lspconfig.nvim", "saghen/blink.cmp" },
    config = function()
      vim.lsp.config("*", { capabilities = require("blink.cmp").get_lsp_capabilities() })
      vim.lsp.config("lua_ls", { settings = { Lua = { hint = { enable = true }, workspace = { checkThirdParty = false } } } })
      vim.lsp.config("pyright", { settings = { pyright = { disableOrganizeImports = true } } })
      vim.lsp.config("clangd", { cmd = { "clangd", "--background-index", "--clang-tidy", "--header-insertion=never" } })

      require("mason-lspconfig").setup({ automatic_enable = true })

      vim.api.nvim_create_autocmd("LspAttach", {
        group = vim.api.nvim_create_augroup("lavender_lsp", { clear = true }),
        callback = function(ev)
          local function map(keys, fn, desc, mode)
            vim.keymap.set(mode or "n", keys, fn, { buffer = ev.buf, desc = desc })
          end
          map("gd", "<cmd>FzfLua lsp_definitions jump1=true<cr>", "Определение")
          map("gD", vim.lsp.buf.declaration, "Объявление")
          map("gr", "<cmd>FzfLua lsp_references jump1=true<cr>", "Где используется")
          map("gI", "<cmd>FzfLua lsp_implementations jump1=true<cr>", "Реализации")
          map("gy", "<cmd>FzfLua lsp_typedefs jump1=true<cr>", "Определение типа")
          map("K", function() vim.lsp.buf.hover({ border = "rounded" }) end, "Документация")
          map("<C-k>", function() vim.lsp.buf.signature_help({ border = "rounded" }) end, "Параметры функции", "i")
          map("<leader>ca", vim.lsp.buf.code_action, "Действия / исправления", { "n", "x" })
          map("<leader>cr", vim.lsp.buf.rename, "Переименовать")
          map("<F2>", vim.lsp.buf.rename, "Переименовать")
          map("<leader>cd", vim.diagnostic.open_float, "Ошибка под курсором")
          map("<leader>cl", "<cmd>checkhealth vim.lsp<cr>", "Информация LSP")
        end,
      })
    end,
  },
}
