--!nonstrict
-- Пилюля заголовка + «остров».
-- Обычно: три точки (закрыть · развернуть · открепить) и название активного окна.
-- На время превращается: громкость / яркость (иконка, полоска, проценты) или новый трек (обложка, название).

local title = ""
local hovered = false

-- остров
local mode = nil          -- nil | "vol" | "bri" | "media"
local untilMs = 0
local target, shown = 0, 0
local muted = false
local media = { text = "", art = nil }
local lastTrack = nil
local artSeq = 0
local BAR = 110

local function niri(action)
  noctalia.runAsync({ "niri", "msg", "action", action })
end

local function bar(p)
  local fillW = math.max(0, math.min(BAR, math.floor(BAR * p + 0.5)))
  return ui.row({ gap = 0, align = "center", width = BAR, height = 4, radius = 2, fill = "on_surface_variant/0.22" }, {
    ui.box({ width = math.max(fillW, 1), height = 4, radius = 2, fill = muted and "on_surface_variant/0.5" or "primary",
             opacity = fillW == 0 and 0 or 1 }),
    ui.spacer({ flexGrow = 1 }),
  })
end

local function islandBody()
  if mode == "vol" or mode == "bri" then
    local icon
    if mode == "bri" then icon = "sun"
    elseif muted or target <= 0 then icon = "volume-off"
    elseif target < 0.5 then icon = "volume-2"
    else icon = "volume" end
    return {
      ui.glyph({ name = icon, size = 15, color = muted and "on_surface_variant" or "primary" }),
      bar(shown),
      ui.label({ text = tostring(math.floor(target * 100 + 0.5)), color = "on_surface_variant", width = 26 }),
    }
  elseif mode == "media" then
    local kids = {}
    if media.art then
      table.insert(kids, ui.image({ key = "art", path = media.art, width = 20, height = 20, radius = 5, fit = "cover" }))
    else
      table.insert(kids, ui.glyph({ name = "music", size = 15, color = "primary" }))
    end
    table.insert(kids, ui.label({ text = media.text, maxWidth = 380, color = "on_surface" }))
    return kids
  end
  return nil
end

local function render()
  local body = islandBody()
  if body then
    barWidget.setVisible(true)
    barWidget.render(ui.row({ key = "pill", fill = "primary/0.16", radius = 999, paddingH = 12, gap = 8, align = "center" }, body))
    return
  end
  if title == "" then
    barWidget.setVisible(false)
    return
  end
  barWidget.setVisible(true)
  local function dot(key, color, action, tip)
    return ui.row({ key = key, paddingH = 3, paddingV = 4, align = "center", tooltip = tip,
                    onClick = function() niri(action) end,
                    onHover = function(state) hovered = state == "true"; render() end }, {
      ui.box({ width = 10, height = 10, radius = 5, fill = hovered and color or "on_surface_variant/0.35" }),
    })
  end
  barWidget.render(ui.row({ key = "pill", fill = "primary/0.12", radius = 999, paddingH = 8, gap = 6, align = "center",
                            onHover = function(state) hovered = state == "true"; render() end }, {
    ui.row({ gap = 0, align = "center" }, {
      dot("close", "error", "close-window", "Закрыть"),
      dot("max", "primary", "maximize-column", "Развернуть"),
      dot("float", "secondary", "toggle-window-floating", "Открепить"),
    }),
    ui.label({ text = title, maxWidth = 340, color = "on_surface" }),
  }))
end

local function open(m, ms)
  mode = m
  untilMs = noctalia.nowMs() + ms
  noctalia.setUpdateInterval(16)
  render()
end

-- громкость и яркость
noctalia.runStream("python3 -I '" .. noctalia.pluginDir() .. "/island.py'", function(line)
  local kind, a, b = string.match(line, "^(%a+) (%d+) ?(%d*)$")
  if not kind then return end
  local v = tonumber(a) / 100
  if kind == "vol" then muted = (b == "1") else muted = false end
  if mode ~= kind then shown = v end
  target = v
  open(kind, 1400)
end)

-- трек: обложка (локальный файл или скачанная копия)
local function setArt(url)
  media.art = nil
  if not url or url == "" then return end
  local path = string.match(url, "^file://(.+)$")
  if path then
    media.art = string.gsub(path, "%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end)
    return
  end
  if string.match(url, "^https?://") then
    local dir = noctalia.pluginDataDir()
    if not dir then return end
    artSeq = (artSeq + 1) % 4
    local dest = dir .. "/art" .. artSeq
    local want = url
    noctalia.download(url, dest, function(ok)
      if ok and mode == "media" and media.url == want then
        media.art = dest
        render()
      end
    end)
  end
end

local function checkMedia()
  local p = noctalia.mediaPlayer()
  if lastTrack == nil then                               -- первый опрос: запомнить, но не показывать
    lastTrack = (p and p.title ~= "") and (p.title .. "|" .. table.concat(p.artists or {}, ", ")) or ""
    return
  end
  if not p or p.title == "" then return end
  local artists = table.concat(p.artists or {}, ", ")
  local key = p.title .. "|" .. artists
  if key ~= lastTrack then
    lastTrack = key
    if p.status ~= "Playing" then return end
    media.text = artists ~= "" and (p.title .. "  ·  " .. artists) or p.title
    media.url = p.artUrl
    setArt(p.artUrl)
    open("media", 4000)
  end
end

local lastMediaPoll = 0
function update()
  local now = noctalia.nowMs()
  if now - lastMediaPoll >= 500 then
    lastMediaPoll = now
    checkMedia()
  end
  if mode then
    if now >= untilMs then
      mode = nil
      noctalia.setUpdateInterval(250)
      render()
      return
    end
    if math.abs(target - shown) > 0.004 then
      shown = shown + (target - shown) * 0.35
      render()
    end
  end
end

noctalia.runStream("python3 -I '" .. noctalia.pluginDir() .. "/focus.py'", function(line)
  title = line
  render()
end)

noctalia.setUpdateInterval(250)
render()
