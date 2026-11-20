# События для «острова»: громкость и яркость. Печатает строки только при изменении (не при старте):
#   vol <проценты> <1 если выключен звук>
#   bri <проценты>
import os, re, subprocess, threading, time, glob

def out(s):
    print(s, flush=True)

def get_vol():
    try:
        r = subprocess.run(["wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"], capture_output=True, text=True, timeout=2).stdout
        m = re.search(r"([\d.]+)", r)
        return (round(float(m.group(1)) * 100), 1 if "MUTED" in r else 0) if m else None
    except Exception:
        return None

def volume_loop():
    last = get_vol()
    while True:
        p = subprocess.Popen(["pactl", "subscribe"], stdout=subprocess.PIPE, text=True)
        for line in p.stdout:
            if "'change' on sink" not in line and "'change' on server" not in line:
                continue
            v = get_vol()
            if v and v != last:
                last = v
                out(f"vol {v[0]} {v[1]}")
        time.sleep(2)

def bright_loop():
    devs = glob.glob("/sys/class/backlight/*")
    if not devs:
        return
    d = devs[0]
    mx = int(open(f"{d}/max_brightness").read())
    def cur():
        return round(int(open(f"{d}/brightness").read()) * 100 / mx)
    last = cur()
    while True:
        time.sleep(0.12)
        try:
            b = cur()
        except Exception:
            continue
        if b != last:
            last = b
            out(f"bri {b}")

threading.Thread(target=bright_loop, daemon=True).start()
volume_loop()
