--!nonstrict
-- Мини-карта колонок niri: блок на колонку (ширина пропорциональна колонке),
-- окна в колонке — друг под другом. Активное окно лиловое; клик по блоку фокусирует окно.

local SCREEN = 1536   -- логическая ширина экрана (1920 / 1.25)
local MAXW, MINW, H = 26, 5, 14
local cols = {}

local function render()
  if #cols == 0 then
    barWidget.setVisible(false)
    return
  end
  barWidget.setVisible(true)
  local children = {}
  for ci, col in ipairs(cols) do
    local w = math.max(MINW, math.min(MAXW, math.floor(col.width / SCREEN * MAXW + 0.5)))
    local n = #col.wins
    local h = math.max(3, math.floor((H - (n - 1) * 2) / n))
    local boxes = {}
    for wi, win in ipairs(col.wins) do
      table.insert(boxes, ui.box({
        key = "w" .. win.id, width = w, height = h, radius = 3,
        fill = win.focused and "primary" or "on_surface_variant/0.35",
        tooltip = win.app,
        onClick = function() noctalia.runAsync({ "niri", "msg", "action", "focus-window", "--id", win.id }) end,
      }))
    end
    table.insert(children, ui.column({ key = "c" .. ci, gap = 2, align = "center" }, boxes))
  end
  barWidget.render(ui.row({ key = "map", fill = "primary/0.12", radius = 999, paddingH = 10, paddingV = 4,
                            gap = 3, align = "center" }, children))
end

local function parse(line)
  local out = {}
  for _, tok in ipairs(string.split(line, " ")) do
    local width, rest = string.match(tok, "^(%d+)=(.+)$")
    if width then
      local col = { width = tonumber(width), wins = {} }
      for _, wt in ipairs(string.split(rest, ",")) do
        local id, app, star = string.match(wt, "^(%d+)~([^*]*)(%*?)$")
        if id then table.insert(col.wins, { id = id, app = app, focused = star == "*" }) end
      end
      table.insert(out, col)
    end
  end
  return out
end

noctalia.runStream("python3 -I '" .. noctalia.pluginDir() .. "/layout.py'", function(line)
  cols = parse(line)
  render()
end)

render()
