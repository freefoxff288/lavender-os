local function palette() return require("lavender.palette") end

return {
  { "nvim-tree/nvim-web-devicons", lazy = true },

  -- стартовый экран, отступы, уведомления, поля ввода
  {
    "folke/snacks.nvim",
    priority = 1000,
    lazy = false,
    opts = {
      bigfile = { enabled = true },
      notifier = { enabled = true, timeout = 2500, style = "compact" },
      input = { enabled = true },
      indent = { enabled = true, animate = { enabled = false } },
      scope = { enabled = true },
      words = { enabled = true },
      dashboard = {
        enabled = true,
        preset = {
          header = table.concat({
            "╭──────────────────────────────╮",
            "│                              │",
            "│    ✿    n e o v i m    ✿     │",
            "│                              │",
            "╰──────────────────────────────╯",
          }, "\n"),
          keys = {
            { icon = "\u{f15b} ", key = "f", desc = "Найти файл", action = ":FzfLua files" },
            { icon = "\u{f15c} ", key = "n", desc = "Новый файл", action = ":ene | startinsert" },
            { icon = "\u{f017} ", key = "r", desc = "Недавние файлы", action = ":FzfLua oldfiles" },
            { icon = "\u{f002} ", key = "g", desc = "Поиск по тексту", action = ":FzfLua live_grep" },
            { icon = "\u{f07b} ", key = "e", desc = "Дерево файлов", action = ":Neotree toggle" },
            { icon = "\u{f0e2} ", key = "s", desc = "Восстановить сессию", action = function() require("persistence").load() end },
            { icon = "󰋖 ", key = "h", desc = "Шпаргалка", action = "<leader>h" },
            { icon = "\u{f013} ", key = "c", desc = "Настройки", action = ":e " .. vim.fn.stdpath("config") .. "/init.lua" },
            { icon = "󰒲 ", key = "l", desc = "Плагины", action = ":Lazy" },
            { icon = "\u{f0ad} ", key = "m", desc = "Языковые серверы", action = ":Mason" },
            { icon = "\u{f011} ", key = "q", desc = "Выход", action = ":qa" },
          },
        },
        sections = {
          { section = "header" },
          { section = "keys", gap = 1, padding = 1 },
          { section = "recent_files", title = "Недавние", indent = 2, padding = 1, limit = 6 },
          { section = "startup" },
        },
      },
    },
  },

  -- строка состояния
  {
    "nvim-lualine/lualine.nvim",
    event = "VeryLazy",
    opts = function()
      local p = palette()
      local function mode(c)
        return { a = { fg = p.bg, bg = c, gui = "bold" }, b = { fg = c, bg = p.sel }, c = { fg = p.sub, bg = p.surf } }
      end
      local theme = {
        normal = mode(p.lav), insert = mode(p.sage), visual = mode(p.pink),
        replace = mode(p.rose), command = mode(p.amber), terminal = mode(p.cyan),
        inactive = { a = { fg = p.muted, bg = p.surf }, b = { fg = p.muted, bg = p.surf }, c = { fg = p.muted, bg = p.surf } },
      }
      return {
        options = {
          theme = theme, globalstatus = true,
          component_separators = { left = "", right = "" },
          section_separators = { left = "\u{e0b4}", right = "\u{e0b6}" },
          disabled_filetypes = { statusline = { "snacks_dashboard" } },
        },
        sections = {
          lualine_a = { { "mode", separator = { left = "\u{e0b6}" }, right_padding = 2 } },
          lualine_b = { "branch", "diff" },
          lualine_c = { { "filename", path = 1, symbols = { modified = " ●", readonly = " \u{f023}" } }, "diagnostics" },
          lualine_x = {
            {
              function()
                local names = {}
                for _, c in ipairs(vim.lsp.get_clients({ bufnr = 0 })) do names[#names + 1] = c.name end
                return #names > 0 and ("\u{f085} " .. table.concat(names, " ")) or ""
              end,
              color = { fg = p.muted },
            },
            "filetype",
          },
          lualine_y = { "progress" },
          lualine_z = { { "location", separator = { right = "\u{e0b4}" }, left_padding = 2 } },
        },
        extensions = { "neo-tree", "lazy", "mason", "toggleterm", "trouble", "fzf", "nvim-dap-ui" },
      }
    end,
  },

  -- вкладки открытых файлов сверху
  {
    "akinsho/bufferline.nvim",
    event = "VeryLazy",
    opts = function()
      local p = palette()
      return {
        options = {
          close_command = function(n) Snacks.bufdelete(n) end,
          diagnostics = "nvim_lsp",
          always_show_bufferline = false,
          separator_style = "thin",
          indicator = { style = "underline" },
          offsets = { { filetype = "neo-tree", text = "\u{f07b}  файлы", highlight = "NeoTreeRootName", text_align = "left" } },
        },
        highlights = {
          fill = { bg = p.deep },
          background = { fg = p.muted, bg = p.deep },
          buffer_visible = { fg = p.sub, bg = p.deep },
          buffer_selected = { fg = p.fg, bg = p.bg, bold = true, italic = false, sp = p.lav },
          separator = { fg = p.deep, bg = p.deep },
          separator_selected = { fg = p.deep, bg = p.bg },
          indicator_selected = { fg = p.lav, bg = p.bg, sp = p.lav },
          modified = { fg = p.amber, bg = p.deep },
          modified_selected = { fg = p.amber, bg = p.bg },
          close_button = { fg = p.muted, bg = p.deep },
          close_button_selected = { fg = p.rose, bg = p.bg },
        },
      }
    end,
  },

  -- подсказки клавиш
  {
    "folke/which-key.nvim",
    event = "VeryLazy",
    opts = {
      preset = "helix",
      delay = 300,
      spec = {
        { "<leader>b", group = "буферы" },
        { "<leader>c", group = "код" },
        { "<leader>d", group = "отладка" },
        { "<leader>f", group = "поиск" },
        { "<leader>g", group = "git" },
        { "<leader>s", group = "сессия" },
        { "<leader>t", group = "терминал" },
        { "<leader>u", group = "переключатели" },
        { "<leader>x", group = "ошибки" },
        { "g", group = "переход" },
        { "[", group = "назад" },
        { "]", group = "вперёд" },
      },
    },
  },
}
