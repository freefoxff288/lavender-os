-- Отладчик: Python (debugpy) и C/C++ (codelldb)
return {
  {
    "mfussenegger/nvim-dap",
    dependencies = {
      { "rcarriga/nvim-dap-ui", dependencies = { "nvim-neotest/nvim-nio" } },
      { "theHamsta/nvim-dap-virtual-text", opts = {} },
    },
    keys = {
      { "<F5>", function() require("dap").continue() end, desc = "Отладка: старт / продолжить" },
      { "<F9>", function() require("dap").toggle_breakpoint() end, desc = "Точка останова" },
      { "<F10>", function() require("dap").step_over() end, desc = "Шаг через" },
      { "<F11>", function() require("dap").step_into() end, desc = "Шаг внутрь" },
      { "<S-F11>", function() require("dap").step_out() end, desc = "Шаг наружу" },
      { "<leader>dc", function() require("dap").continue() end, desc = "Старт / продолжить" },
      { "<leader>db", function() require("dap").toggle_breakpoint() end, desc = "Точка останова" },
      { "<leader>dB", function() require("dap").set_breakpoint(vim.fn.input("Условие: ")) end, desc = "Условная точка" },
      { "<leader>dn", function() require("dap").step_over() end, desc = "Шаг через" },
      { "<leader>di", function() require("dap").step_into() end, desc = "Шаг внутрь" },
      { "<leader>do", function() require("dap").step_out() end, desc = "Шаг наружу" },
      { "<leader>dr", function() require("dap").run_to_cursor() end, desc = "До курсора" },
      { "<leader>dq", function() require("dap").terminate() end, desc = "Остановить" },
      { "<leader>du", function() require("dapui").toggle() end, desc = "Панели отладки" },
      { "<leader>de", function() require("dapui").eval() end, desc = "Значение выражения", mode = { "n", "x" } },
    },
    config = function()
      local dap, dapui = require("dap"), require("dapui")
      local mason = vim.fn.stdpath("data") .. "/mason/packages"

      vim.fn.sign_define("DapBreakpoint", { text = "●", texthl = "DapBreakpoint" })
      vim.fn.sign_define("DapBreakpointCondition", { text = "◆", texthl = "DapBreakpoint" })
      vim.fn.sign_define("DapStopped", { text = "▶", texthl = "DapStopped", linehl = "DapStoppedLine" })

      dap.adapters.python = {
        type = "executable",
        command = mason .. "/debugpy/venv/bin/python",
        args = { "-m", "debugpy.adapter" },
      }
      dap.configurations.python = {
        { type = "python", request = "launch", name = "Текущий файл", program = "${file}",
          console = "integratedTerminal", pythonPath = function()
            local venv = vim.env.VIRTUAL_ENV or (vim.fn.getcwd() .. "/.venv")
            return vim.fn.executable(venv .. "/bin/python") == 1 and venv .. "/bin/python" or "python3"
          end },
      }

      dap.adapters.codelldb = { type = "executable", command = mason .. "/codelldb/codelldb" }
      local function build_and_pick()
        local src = vim.fn.expand("%:p")
        local out = vim.fn.expand("%:p:r")
        if vim.fn.confirm("Собрать " .. vim.fn.expand("%:t") .. " с -g?", "&Да\n&Нет, выбрать файл", 1) == 1 then
          local cc = vim.bo.filetype == "cpp" and "g++" or "gcc"
          local res = vim.fn.system({ cc, "-g", "-O0", src, "-o", out, "-lm" })
          if vim.v.shell_error ~= 0 then
            vim.notify(res, vim.log.levels.ERROR)
            return dap.ABORT
          end
          return out
        end
        return vim.fn.input("Программа: ", vim.fn.getcwd() .. "/", "file")
      end
      dap.configurations.c = {
        { type = "codelldb", request = "launch", name = "Собрать и отладить", program = build_and_pick,
          cwd = "${workspaceFolder}", stopOnEntry = false },
      }
      dap.configurations.cpp = dap.configurations.c

      dapui.setup({ floating = { border = "rounded" } })
      dap.listeners.after.event_initialized.dapui = function() dapui.open() end
      dap.listeners.before.event_terminated.dapui = function() dapui.close() end
      dap.listeners.before.event_exited.dapui = function() dapui.close() end
    end,
  },
}
