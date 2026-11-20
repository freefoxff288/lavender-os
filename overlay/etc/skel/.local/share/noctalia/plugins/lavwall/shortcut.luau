--!nonstrict
-- Плитка Control Center: lavwall random --apply
local busy = false
local LAVWALL = "/usr/local/bin/lavwall"

local function render()
  shortcut.setLabel(if busy then "Рисую…" else "Новые обои")
  shortcut.setIcon("dice-5")
  shortcut.setActive(busy)
  shortcut.setEnabled(not busy)
end

render()

function onClick()
  if busy then return end
  busy = true
  render()
  noctalia.runAsync(LAVWALL .. " random --apply", function(res)
    busy = false
    render()
    if res.exitCode ~= 0 then
      noctalia.notifyError("Обои", res.stderr ~= "" and res.stderr or "lavwall завершился с ошибкой")
    end
  end, 20000)
end

function onRightClick()
  noctalia.runAsync(LAVWALL)
end
