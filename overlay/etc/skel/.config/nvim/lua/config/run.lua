-- <Пробел>r — запустить текущий файл во встроенном терминале
local M = {}
local tmp = (vim.env.XDG_RUNTIME_DIR or "/tmp") .. "/nvim-run"

local runners = {
  python = "python3 %s",
  javascript = "node %s",
  typescript = "npx --yes tsx %s",
  sh = "bash %s",
  bash = "bash %s",
  zsh = "zsh %s",
  lua = "nvim -l %s",
  c = "gcc -Wall -g %s -o " .. tmp .. " -lm && " .. tmp,
  cpp = "g++ -Wall -g %s -o " .. tmp .. " && " .. tmp,
  go = "go run %s",
  rust = "cargo run",
  java = "java %s",
  html = "xdg-open %s",
  make = "make",
}

function M.run()
  local ft = vim.bo.filetype
  local file = vim.fn.expand("%:p")
  local tpl = runners[ft]
  if not tpl then
    if vim.fn.executable(file) == 1 then
      tpl = "%s"
    else
      return vim.notify("Не знаю, как запускать «" .. ft .. "»", vim.log.levels.WARN)
    end
  end
  vim.cmd("silent! write")
  local cmd = tpl:format(vim.fn.shellescape(file))
  local Terminal = require("toggleterm.terminal").Terminal
  if M.term then M.term:shutdown() end
  M.term = Terminal:new({
    cmd = cmd,
    dir = vim.fn.expand("%:p:h"),
    direction = "horizontal",
    close_on_exit = false,
    on_exit = function(t, _, code)
      vim.schedule(function()
        if vim.api.nvim_buf_is_valid(t.bufnr) then
          vim.notify(code == 0 and "✓ готово" or ("✗ код выхода " .. code),
            code == 0 and vim.log.levels.INFO or vim.log.levels.ERROR)
        end
      end)
    end,
  })
  M.term:toggle(14)
end

return M
