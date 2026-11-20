return {
  -- автодополнение
  {
    "saghen/blink.cmp",
    version = "1.*",
    event = { "InsertEnter", "CmdlineEnter" },
    dependencies = { "rafamadriz/friendly-snippets" },
    opts = {
      keymap = {
        preset = "enter", -- Enter — принять, Tab/Shift+Tab — выбор
        ["<Tab>"] = { "select_next", "snippet_forward", "fallback" },
        ["<S-Tab>"] = { "select_prev", "snippet_backward", "fallback" },
      },
      appearance = { nerd_font_variant = "mono" },
      completion = {
        list = { selection = { preselect = true, auto_insert = false } },
        menu = { border = "rounded", draw = { treesitter = { "lsp" },
          columns = { { "kind_icon" }, { "label", "label_description", gap = 1 }, { "kind" } } } },
        documentation = { auto_show = true, auto_show_delay_ms = 250, window = { border = "rounded" } },
        ghost_text = { enabled = true },
      },
      signature = { enabled = true, window = { border = "rounded" } },
      sources = {
        default = { "lazydev", "lsp", "path", "snippets", "buffer" },
        providers = { lazydev = { name = "LazyDev", module = "lazydev.integrations.blink", score_offset = 100 } },
      },
      cmdline = { completion = { menu = { auto_show = true } } },
      fuzzy = { implementation = "prefer_rust_with_warning" },
    },
  },

  -- форматирование кода
  {
    "stevearc/conform.nvim",
    event = "BufWritePre",
    cmd = "ConformInfo",
    keys = {
      { "<leader>cf", function() require("conform").format({ async = true }) end, mode = { "n", "x" }, desc = "Отформатировать" },
      {
        "<leader>uf",
        function()
          vim.g.format_on_save = not vim.g.format_on_save
          vim.notify("Форматирование при сохранении: " .. (vim.g.format_on_save and "вкл" or "выкл"))
        end,
        desc = "Форматировать при сохранении",
      },
    },
    opts = {
      formatters_by_ft = {
        lua = { "stylua" },
        python = { "ruff_organize_imports", "ruff_format" },
        javascript = { "prettier" }, typescript = { "prettier" },
        javascriptreact = { "prettier" }, typescriptreact = { "prettier" },
        css = { "prettier" }, scss = { "prettier" }, html = { "prettier" },
        json = { "prettier" }, jsonc = { "prettier" }, yaml = { "prettier" }, markdown = { "prettier" },
        sh = { "shfmt" }, bash = { "shfmt" },
        c = { "clang-format" }, cpp = { "clang-format" },
        toml = { "taplo" },
      },
      default_format_opts = { lsp_format = "fallback" },
      format_on_save = function()
        if vim.g.format_on_save then return { timeout_ms = 1500 } end
      end,
    },
  },
}
