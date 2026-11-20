# Печатает 1, когда на активном рабочем столе (фокусный монитор) нет окон, иначе 0. Только при изменении.
import json, subprocess, time, os
LOG = open(os.path.expanduser("~/.cache/desk-empty.log"), "a", buffering=1)
p = subprocess.Popen(["niri", "msg", "--json", "event-stream"], stdout=subprocess.PIPE, text=True)
wss, last = {}, None
evname = ""
def emit():
    global last
    ws = next((w for w in wss.values() if w.get("is_focused")), None)
    v = "1" if ws is not None and ws.get("active_window_id") is None else "0"
    LOG.write(f"{time.strftime('%H:%M:%S')}.{int(time.time()*1000)%1000:03d} ev={evname} ws={ws and ws['id']} aw={ws and ws.get('active_window_id')} -> {v}{' EMIT' if v != last else ''}\n")
    if v != last:
        last = v
        print(v, flush=True)
for raw in p.stdout:
    try: ev = json.loads(raw)
    except ValueError: continue
    evname = next(iter(ev), "?")
    if "WorkspacesChanged" in ev:
        wss = {w["id"]: w for w in ev["WorkspacesChanged"]["workspaces"]}
    elif "WorkspaceActivated" in ev:
        a = ev["WorkspaceActivated"]
        if a.get("focused"):
            for w in wss.values(): w["is_focused"] = (w["id"] == a["id"])
    elif "WorkspaceActiveWindowChanged" in ev:
        a = ev["WorkspaceActiveWindowChanged"]
        if a["workspace_id"] in wss: wss[a["workspace_id"]]["active_window_id"] = a["active_window_id"]
    else:
        continue
    emit()
