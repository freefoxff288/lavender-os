# Печатает раскладку активного рабочего стола niri при каждом изменении.
# Формат строки: колонки через пробел; колонка = "<ширина_px>=<окно>,<окно>"; окно = "<id>~<app_id>[*]" (* — в фокусе).
import json, subprocess
p = subprocess.Popen(["niri", "msg", "--json", "event-stream"], stdout=subprocess.PIPE, text=True)
wins, wss, focus, last = {}, {}, None, None

def active_ws():
    for w in wss.values():
        if w.get("is_focused"): return w["id"]
    return None

def emit():
    global last
    ws = active_ws()
    cols = {}
    for w in wins.values():
        lay = w.get("layout") or {}
        pos = lay.get("pos_in_scrolling_layout")
        if w.get("workspace_id") != ws or w.get("is_floating") or not pos: continue
        cols.setdefault(pos[0], []).append((pos[1], w))
    parts = []
    for c in sorted(cols):
        tiles = [w for _, w in sorted(cols[c], key=lambda t: t[0])]
        width = int(max((t.get("layout") or {}).get("tile_size", [0, 0])[0] for t in tiles))
        names = []
        for t in tiles:
            app = (t.get("app_id") or "окно").replace(" ", "_").replace(",", "_").replace("=", "_").replace("~", "_").replace("*", "_")
            names.append(f'{t["id"]}~{app}' + ("*" if t["id"] == focus else ""))
        parts.append(f"{width}=" + ",".join(names))
    line = " ".join(parts)
    if line != last:
        last = line
        print(line, flush=True)

for raw in p.stdout:
    try: ev = json.loads(raw)
    except ValueError: continue
    if "WindowsChanged" in ev:
        wins = {w["id"]: w for w in ev["WindowsChanged"]["windows"]}
        focus = next((i for i, w in wins.items() if w.get("is_focused")), focus)
    elif "WindowOpenedOrChanged" in ev:
        w = ev["WindowOpenedOrChanged"]["window"]; wins[w["id"]] = w
        if w.get("is_focused"): focus = w["id"]
    elif "WindowClosed" in ev:
        wins.pop(ev["WindowClosed"]["id"], None)
    elif "WindowFocusChanged" in ev:
        focus = ev["WindowFocusChanged"]["id"]
    elif "WindowLayoutsChanged" in ev:
        for wid, lay in ev["WindowLayoutsChanged"]["changes"]:
            if wid in wins: wins[wid]["layout"] = lay
    elif "WorkspacesChanged" in ev:
        wss = {w["id"]: w for w in ev["WorkspacesChanged"]["workspaces"]}
    elif "WorkspaceActivated" in ev:
        a = ev["WorkspaceActivated"]
        if a.get("focused"):
            for w in wss.values(): w["is_focused"] = (w["id"] == a["id"])
    else:
        continue
    emit()
