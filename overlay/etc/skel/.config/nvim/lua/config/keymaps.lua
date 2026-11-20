local map = vim.keymap.set

-- сохранение / выход
map({ "n", "i", "x" }, "<C-s>", "<cmd>w<cr><esc>", { desc = "Сохранить" })
map("n", "<leader>w", "<cmd>w<cr>", { desc = "Сохранить" })
map("n", "<leader>q", "<cmd>qa<cr>", { desc = "Выйти из всего" })
map("n", "<Esc>", "<cmd>nohlsearch<cr>", { desc = "Убрать подсветку поиска" })

-- окна
map("n", "<C-h>", "<C-w>h", { desc = "Окно слева" })
map("n", "<C-j>", "<C-w>j", { desc = "Окно снизу" })
map("n", "<C-k>", "<C-w>k", { desc = "Окно сверху" })
map("n", "<C-l>", "<C-w>l", { desc = "Окно справа" })
map("n", "<leader>-", "<cmd>split<cr>", { desc = "Разделить горизонтально" })
map("n", "<leader>|", "<cmd>vsplit<cr>", { desc = "Разделить вертикально" })
map("n", "<C-Up>", "<cmd>resize +2<cr>", { desc = "Выше" })
map("n", "<C-Down>", "<cmd>resize -2<cr>", { desc = "Ниже" })
map("n", "<C-Left>", "<cmd>vertical resize -2<cr>", { desc = "Уже" })
map("n", "<C-Right>", "<cmd>vertical resize +2<cr>", { desc = "Шире" })

-- вкладки-буферы
map("n", "<S-h>", "<cmd>bprevious<cr>", { desc = "Предыдущий буфер" })
map("n", "<S-l>", "<cmd>bnext<cr>", { desc = "Следующий буфер" })
map("n", "<leader>bd", function() Snacks.bufdelete() end, { desc = "Закрыть буфер" })
map("n", "<leader>bo", function() Snacks.bufdelete.other() end, { desc = "Закрыть остальные" })

-- редактирование
map("n", "<A-j>", "<cmd>m .+1<cr>==", { desc = "Строку вниз" })
map("n", "<A-k>", "<cmd>m .-2<cr>==", { desc = "Строку вверх" })
map("x", "<A-j>", ":m '>+1<cr>gv=gv", { desc = "Выделение вниз", silent = true })
map("x", "<A-k>", ":m '<-2<cr>gv=gv", { desc = "Выделение вверх", silent = true })
map("x", "<", "<gv")
map("x", ">", ">gv")
map("n", "<leader>a", "ggVG", { desc = "Выделить всё" })

-- запуск текущего файла
map("n", "<leader>r", function() require("config.run").run() end, { desc = "Запустить файл" })

-- переключатели
map("n", "<leader>us", "<cmd>setlocal spell!<cr>", { desc = "Проверка орфографии" })
map("n", "<leader>uw", "<cmd>setlocal wrap!<cr>", { desc = "Перенос строк" })
map("n", "<leader>un", function() vim.o.relativenumber = not vim.o.relativenumber end,
  { desc = "Относительные номера" })
map("n", "<leader>ui", function()
  vim.lsp.inlay_hint.enable(not vim.lsp.inlay_hint.is_enabled())
end, { desc = "Подсказки типов (inlay)" })

-- шпаргалка
map("n", "<leader>h", function()
  Snacks.win({
    file = vim.fn.stdpath("config") .. "/CHEATSHEET.md",
    width = 0.7, height = 0.85, border = "rounded", title = " шпаргалка ",
    wo = { conceallevel = 2, spell = false, wrap = true },
    bo = { modifiable = false },
    keys = { q = "close" },
  })
end, { desc = "Шпаргалка" })

-- терминал: выйти в обычный режим
map("t", "<Esc><Esc>", [[<C-\><C-n>]], { desc = "Режим команд в терминале" })
