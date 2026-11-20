return {
  -- дерево файлов
  {
    "nvim-neo-tree/neo-tree.nvim",
    branch = "v3.x",
    cmd = "Neotree",
    dependencies = { "nvim-lua/plenary.nvim", "MunifTanjim/nui.nvim", "nvim-tree/nvim-web-devicons" },
    keys = {
      { "<leader>e", "<cmd>Neotree toggle reveal<cr>", desc = "Дерево файлов" },
      { "<leader>E", "<cmd>Neotree float git_status<cr>", desc = "Изменения git" },
    },
    init = function()
      -- `nvim папка` сразу открывает дерево
      vim.api.nvim_create_autocmd("BufEnter", {
        group = vim.api.nvim_create_augroup("neotree_start", { clear = true }),
        desc = "Открыть neo-tree для папки",
        callback = function()
          local f = vim.fn.expand("%")
          if f ~= "" and vim.fn.isdirectory(f) == 1 then
            vim.api.nvim_del_augroup_by_name("neotree_start")
            require("neo-tree")
          end
        end,
      })
    end,
    opts = {
      close_if_last_window = true,
      popup_border_style = "rounded",
      filesystem = {
        hijack_netrw_behavior = "open_current",
        follow_current_file = { enabled = true },
        use_libuv_file_watcher = true,
        filtered_items = { visible = true, hide_dotfiles = false, hide_gitignored = false,
          never_show = { ".git" } },
      },
      window = {
        width = 32,
        mappings = {
          ["<space>"] = "none",
          ["l"] = "open",
          ["h"] = "close_node",
          ["a"] = { "add", config = { show_path = "relative" } },
        },
      },
      default_component_configs = {
        indent = { with_expanders = true },
        git_status = { symbols = { added = "+", modified = "~", deleted = "−", renamed = "→",
          untracked = "?", ignored = "◌", unstaged = "\u{f10c}", staged = "\u{f00c}", conflict = "\u{e727}" } },
      },
    },
  },

  -- поиск всего: файлы, текст, символы
  {
    "ibhagwan/fzf-lua",
    cmd = "FzfLua",
    dependencies = { "nvim-tree/nvim-web-devicons" },
    keys = {
      { "<leader><space>", "<cmd>FzfLua files<cr>", desc = "Найти файл" },
      { "<leader>ff", "<cmd>FzfLua files<cr>", desc = "Файлы" },
      { "<leader>fg", "<cmd>FzfLua live_grep<cr>", desc = "Текст в проекте" },
      { "<leader>fw", "<cmd>FzfLua grep_cword<cr>", desc = "Слово под курсором" },
      { "<leader>fr", "<cmd>FzfLua oldfiles<cr>", desc = "Недавние" },
      { "<leader>fb", "<cmd>FzfLua buffers<cr>", desc = "Открытые буферы" },
      { "<leader>fh", "<cmd>FzfLua helptags<cr>", desc = "Справка vim" },
      { "<leader>fk", "<cmd>FzfLua keymaps<cr>", desc = "Все клавиши" },
      { "<leader>fc", "<cmd>FzfLua commands<cr>", desc = "Команды" },
      { "<leader>f/", "<cmd>FzfLua blines<cr>", desc = "Строки в файле" },
      { "<leader>fs", "<cmd>FzfLua lsp_document_symbols<cr>", desc = "Символы файла" },
      { "<leader>fS", "<cmd>FzfLua lsp_live_workspace_symbols<cr>", desc = "Символы проекта" },
      { "<leader>fd", "<cmd>FzfLua diagnostics_workspace<cr>", desc = "Ошибки проекта" },
      { "<leader>fR", "<cmd>FzfLua resume<cr>", desc = "Продолжить поиск" },
      { "<leader>gb", "<cmd>FzfLua git_branches<cr>", desc = "Ветки" },
      { "<leader>gl", "<cmd>FzfLua git_commits<cr>", desc = "Коммиты" },
      { "<leader>gs", "<cmd>FzfLua git_status<cr>", desc = "Статус" },
    },
    opts = {
      "default-title",
      fzf_colors = true,
      winopts = { border = "rounded", backdrop = 80, preview = { border = "rounded", layout = "flex" } },
      files = { prompt = "файлы › ", cwd_prompt = false },
      grep = { prompt = "текст › " },
    },
    config = function(_, opts)
      require("fzf-lua").setup(opts)
      require("fzf-lua").register_ui_select()
    end,
  },

  -- git: изменения слева от номеров строк
  {
    "lewis6991/gitsigns.nvim",
    event = { "BufReadPre", "BufNewFile" },
    opts = {
      signs = { add = { text = "▎" }, change = { text = "▎" }, delete = { text = "\u{f0da}" },
        topdelete = { text = "\u{f0da}" }, changedelete = { text = "▎" }, untracked = { text = "┆" } },
      on_attach = function(buf)
        local gs = require("gitsigns")
        local function map(l, r, d) vim.keymap.set("n", l, r, { buffer = buf, desc = d }) end
        map("]h", function() gs.nav_hunk("next") end, "Следующее изменение")
        map("[h", function() gs.nav_hunk("prev") end, "Предыдущее изменение")
        map("<leader>gp", gs.preview_hunk, "Показать изменение")
        map("<leader>gr", gs.reset_hunk, "Откатить изменение")
        map("<leader>ga", gs.stage_hunk, "Добавить изменение (stage)")
        map("<leader>gB", function() gs.blame_line({ full = true }) end, "Кто написал строку")
        map("<leader>gd", gs.diffthis, "Diff файла")
      end,
    },
  },

  -- список ошибок / ссылок
  {
    "folke/trouble.nvim",
    cmd = "Trouble",
    opts = { focus = true },
    keys = {
      { "<leader>xx", "<cmd>Trouble diagnostics toggle<cr>", desc = "Ошибки проекта" },
      { "<leader>xb", "<cmd>Trouble diagnostics toggle filter.buf=0<cr>", desc = "Ошибки файла" },
      { "<leader>xs", "<cmd>Trouble symbols toggle win.position=right<cr>", desc = "Структура файла" },
      { "<leader>xt", "<cmd>Trouble todo toggle<cr>", desc = "TODO в проекте" },
    },
  },

  -- терминал внутри редактора
  {
    "akinsho/toggleterm.nvim",
    version = "*",
    keys = {
      { [[<C-\>]], desc = "Терминал" },
      { "<leader>tf", "<cmd>ToggleTerm direction=float<cr>", desc = "Плавающий терминал" },
      { "<leader>th", "<cmd>ToggleTerm size=14 direction=horizontal<cr>", desc = "Терминал снизу" },
      { "<leader>tv", "<cmd>ToggleTerm size=70 direction=vertical<cr>", desc = "Терминал справа" },
    },
    cmd = { "ToggleTerm", "TermExec" },
    opts = {
      open_mapping = [[<C-\>]],
      direction = "float",
      shade_terminals = false,
      float_opts = { border = "rounded", winblend = 0 },
      highlights = { FloatBorder = { link = "ToggleTermBorder" }, NormalFloat = { link = "NormalFloat" } },
    },
  },

  -- сессии: вернуть открытые файлы проекта
  {
    "folke/persistence.nvim",
    event = "BufReadPre",
    opts = {},
    keys = {
      { "<leader>ss", function() require("persistence").load() end, desc = "Восстановить сессию" },
      { "<leader>sl", function() require("persistence").load({ last = true }) end, desc = "Последняя сессия" },
      { "<leader>sd", function() require("persistence").stop() end, desc = "Не сохранять сессию" },
    },
  },

  -- подсветка TODO/FIXME/NOTE
  {
    "folke/todo-comments.nvim",
    event = { "BufReadPost", "BufNewFile" },
    dependencies = { "nvim-lua/plenary.nvim" },
    opts = { signs = false },
    keys = {
      { "]t", function() require("todo-comments").jump_next() end, desc = "Следующий TODO" },
      { "[t", function() require("todo-comments").jump_prev() end, desc = "Предыдущий TODO" },
      { "<leader>ft", "<cmd>TodoFzfLua<cr>", desc = "TODO" },
    },
  },

  -- автоскобки и обрамление (sa" — обернуть в кавычки, sd" — убрать, sr"' — заменить)
  { "echasnovski/mini.pairs", event = "InsertEnter", opts = {} },
  { "echasnovski/mini.surround", event = "VeryLazy", opts = {} },
  { "echasnovski/mini.ai", event = "VeryLazy", opts = {} },
}
