--!nonstrict
-- Пустой рабочий стол: часы, дата и текущий трек. Плавно появляется, когда окон нет,
-- и гаснет, как только открывается окно.

local empty = false
local alpha, targetAlpha = 0, 0
local lastMinute = ""
local media = nil            -- { title, artists, art, playing }
local mediaKey = ""
local artSeq = 0

local function render()
  if alpha <= 0.01 then
    desktopWidget.setVisible(false)
    return
  end
  desktopWidget.setVisible(true)
  local now = os.time()
  local rows = {
    ui.label({ text = noctalia.formatTime("{:%H:%M}", now), fontSize = 112, fontWeight = "thin",
               color = "on_surface", shadow = true }),
    ui.label({ text = noctalia.formatTime("{:%A, %e %B}", now), fontSize = 20, fontWeight = "light",
               color = "primary", shadow = true }),
  }
  if media then
    local info = {
      ui.label({ text = media.title, fontSize = 15, fontWeight = "medium", color = "on_surface", maxWidth = 320, shadow = true }),
    }
    if media.artists ~= "" then
      table.insert(info, ui.label({ text = media.artists, fontSize = 13, color = "on_surface_variant", maxWidth = 320, shadow = true }))
    end
    local row = {}
    if media.art then
      table.insert(row, ui.image({ key = "art", path = media.art, width = 48, height = 48, radius = 12, fit = "cover" }))
    else
      table.insert(row, ui.row({ width = 48, height = 48, radius = 12, fill = "primary/0.18", align = "center", justify = "center" }, {
        ui.glyph({ name = "music", size = 20, color = "primary" }),
      }))
    end
    table.insert(row, ui.column({ gap = 2, justify = "center" }, info))
    table.insert(rows, ui.row({ key = "media", gap = 12, align = "center", padding = 10, paddingH = 14, radius = 18,
                                fill = "surface/0.55", opacity = media.playing and 1 or 0.6 }, row))
  end
  desktopWidget.render(ui.column({ key = "desk", gap = 14, align = "center", opacity = alpha }, rows))
end

local function setArt(url)
  if not media then return end
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
    local want = mediaKey
    noctalia.download(url, dest, function(ok)
      if ok and media and mediaKey == want then media.art = dest; render() end
    end)
  end
end

local function pollMedia()
  local p = noctalia.mediaPlayer()
  if not p or p.title == "" or p.status == "Stopped" then
    if media then media = nil; mediaKey = ""; return true end
    return false
  end
  local artists = table.concat(p.artists or {}, ", ")
  local key = p.title .. "|" .. artists .. "|" .. (p.artUrl or "")
  local playing = p.status == "Playing"
  if key ~= mediaKey then
    mediaKey = key
    media = { title = p.title, artists = artists, playing = playing }
    setArt(p.artUrl)
    return true
  end
  if media.playing ~= playing then media.playing = playing; return true end
  return false
end

-- раз в секунду: часы и трек
function update()
  if not empty and alpha <= 0.01 then return end
  local changed = pollMedia()
  local m = noctalia.formatTime("{:%H:%M}", os.time())
  if m ~= lastMinute then lastMinute = m; changed = true end
  if changed then render() end
end

-- плавное появление / исчезновение
function onFrameTick(deltaMs)
  local step = (deltaMs or 16) / (targetAlpha > alpha and 420 or 220)
  if targetAlpha > alpha then alpha = math.min(targetAlpha, alpha + step)
  else alpha = math.max(targetAlpha, alpha - step) end
  render()
  if alpha == targetAlpha then desktopWidget.setNeedsFrameTick(false) end
end

noctalia.runStream("python3 -I '" .. noctalia.pluginDir() .. "/empty.py'", function(line)
  empty = (line == "1")
  targetAlpha = empty and 1 or 0
  if empty then pollMedia() end
  desktopWidget.setNeedsFrameTick(true)
end)

desktopWidget.setWantsSecondTicks(true)
render()
