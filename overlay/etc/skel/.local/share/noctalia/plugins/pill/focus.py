# Печатает название активного окна niri при каждой смене (пустая строка — окна нет).
import json, subprocess, sys
p = subprocess.Popen(["niri", "msg", "--json", "event-stream"], stdout=subprocess.PIPE, text=True)
wins, focus, last = {}, None, None
def emit():
    global last
    w = wins.get(focus)
    t = ((w.get("title") or w.get("app_id") or "") if w else "").replace("\n", " ").strip()
    if t != last:
        last = t
        print(t, flush=True)
for line in p.stdout:
    try: ev = json.loads(line)
    except ValueError: continue
    if "WindowsChanged" in ev:
        wins = {w["id"]: w for w in ev["WindowsChanged"]["windows"]}
        focus = next((i for i, w in wins.items() if w.get("is_focused")), None)
    elif "WindowOpenedOrChanged" in ev:
        w = ev["WindowOpenedOrChanged"]["window"]; wins[w["id"]] = w
        if w.get("is_focused"): focus = w["id"]
    elif "WindowClosed" in ev:
        wins.pop(ev["WindowClosed"]["id"], None)
    elif "WindowFocusChanged" in ev:
        focus = ev["WindowFocusChanged"]["id"]
    else:
        continue
    emit()
