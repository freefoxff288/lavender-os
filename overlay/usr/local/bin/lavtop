#!/usr/bin/env python3
"""lavtop — мягкий лавандовый диспетчер задач для терминала.

Читает /proc напрямую, без зависимостей (только stdlib + curses).
"""
import curses
import locale
import os
import pwd
import signal
import socket
import time

HZ = os.sysconf("SC_CLK_TCK")
PAGE = os.sysconf("SC_PAGE_SIZE")
NCPU = os.cpu_count() or 1
ME = os.getuid()
INTERVAL = 1.5
SPARK = "▁▂▃▄▅▆▇█"

SORTS = {  # key -> (заголовок колонки, функция, по убыванию)
    "cpu": ("CPU%", lambda p: p.cpu, True),
    "mem": ("MEM%", lambda p: p.rss, True),
    "pid": ("PID", lambda p: p.pid, False),
    "name": ("COMMAND", lambda p: p.name.lower(), False),
    "time": ("TIME", lambda p: p.ticks, True),
}

SIGNALS = [("1", "TERM", signal.SIGTERM), ("2", "KILL", signal.SIGKILL),
           ("3", "HUP", signal.SIGHUP), ("4", "INT", signal.SIGINT),
           ("5", "STOP", signal.SIGSTOP), ("6", "CONT", signal.SIGCONT)]

STATES = {"R": "работает", "S": "спит", "D": "ждёт I/O", "Z": "зомби",
          "T": "остановлен", "t": "трассируется", "I": "простой", "X": "мёртв"}


def read(path):
    try:
        with open(path, "rb") as f:
            return f.read().decode(errors="replace")
    except OSError:
        return ""


def human(n):
    for unit in ("B", "K", "M", "G", "T"):
        if abs(n) < 1024 or unit == "T":
            return f"{n:.0f}{unit}" if unit in ("B", "K") else f"{n:.1f}{unit}"
        n /= 1024


def dur(sec):
    sec = int(sec)
    d, sec = divmod(sec, 86400)
    h, sec = divmod(sec, 3600)
    m, s = divmod(sec, 60)
    if d:
        return f"{d}д {h}ч"
    if h:
        return f"{h}ч {m:02d}м"
    return f"{m}м {s:02d}с"


def cputime(ticks):
    s = ticks / HZ
    m, s = divmod(s, 60)
    h, m = divmod(int(m), 60)
    return f"{h}:{m:02d}:{s:02.0f}" if h else f"{m}:{s:05.2f}"


# ── сбор данных ──────────────────────────────────────────────

def cpu_times():
    out = []
    for line in read("/proc/stat").splitlines():
        if not line.startswith("cpu"):
            break
        v = list(map(int, line.split()[1:9]))
        out.append((sum(v), v[3] + v[4]))
    return out


def meminfo():
    m = {}
    for line in read("/proc/meminfo").splitlines():
        k, _, v = line.partition(":")
        v = v.split()
        if v:
            m[k] = int(v[0]) * 1024
    return m


def find_sensors():
    temp = bat = None
    base = "/sys/class/hwmon"
    try:
        for h in sorted(os.listdir(base)):
            name = read(f"{base}/{h}/name").strip()
            if name in ("k10temp", "coretemp", "zenpower") and temp is None:
                temp = f"{base}/{h}/temp1_input"
    except OSError:
        pass
    ps = "/sys/class/power_supply"
    try:
        for b in sorted(os.listdir(ps)):
            if b.startswith("BAT"):
                bat = f"{ps}/{b}"
                break
    except OSError:
        pass
    return temp, bat


_users = {}


def username(uid):
    if uid not in _users:
        try:
            _users[uid] = pwd.getpwuid(uid).pw_name
        except KeyError:
            _users[uid] = str(uid)
    return _users[uid]


class Proc:
    __slots__ = ("pid", "ppid", "name", "state", "uid", "user", "cmd", "nice",
                 "threads", "rss", "ticks", "start", "cpu", "kthread")


_cmd_cache = {}


def scan():
    procs = {}
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        s = read(f"/proc/{d}/stat")
        if not s:
            continue
        try:
            uid = os.stat(f"/proc/{d}").st_uid
        except OSError:
            continue
        r = s.rfind(")")
        f = s[r + 2:].split()
        p = Proc()
        p.pid = int(d)
        p.name = s[s.find("(") + 1:r]
        p.state = f[0]
        p.ppid = int(f[1])
        p.ticks = int(f[11]) + int(f[12])
        p.nice = int(f[16])
        p.threads = int(f[17])
        p.start = int(f[19])
        p.rss = int(f[21]) * PAGE
        p.uid = uid
        p.user = username(uid)
        p.cpu = 0.0
        key = (p.pid, p.start)
        if key not in _cmd_cache:
            _cmd_cache[key] = read(f"/proc/{d}/cmdline").replace("\0", " ").strip()
        p.cmd = _cmd_cache[key] or f"[{p.name}]"
        p.kthread = p.pid == 2 or p.ppid == 2
        procs[p.pid] = p
    alive = {(p.pid, p.start) for p in procs.values()}
    for k in list(_cmd_cache):
        if k not in alive:
            del _cmd_cache[k]
    return procs


# ── приложение ───────────────────────────────────────────────

class App:
    def __init__(self, scr):
        self.scr = scr
        self.host = socket.gethostname()
        self.temp_path, self.bat_path = find_sensors()
        self.prev_cpu = None
        self.prev_ticks = {}
        self.cores = [0.0] * NCPU
        self.total = 0.0
        self.hist = []
        self.procs = {}
        self.mem = {}
        self.sort = "cpu"
        self.reverse = False
        self.tree = False
        self.only_me = False
        self.kthreads = False
        self.filter = ""
        self.mode = "list"  # list | filter | kill | details | help
        self.sel_pid = None
        self.sel_idx = 0
        self.top = 0
        self.rows = []
        self.list_y = 0
        self.paused = False
        self.msg = ("", 0.0)
        self.last = 0.0
        self.sample()
        time.sleep(0.3)
        self.sample()

    # данные
    def sample(self):
        ct = cpu_times()
        if self.prev_cpu and len(ct) == len(self.prev_cpu):
            pct = []
            for (t, i), (pt, pi) in zip(ct, self.prev_cpu):
                dt = t - pt
                pct.append(100.0 * (1 - (i - pi) / dt) if dt > 0 else 0.0)
            self.total, self.cores = pct[0], pct[1:]
            self.hist.append(self.total)
            del self.hist[:-400]
            jiffies = (ct[0][0] - self.prev_cpu[0][0]) / NCPU
        else:
            jiffies = 0
        self.prev_cpu = ct
        procs = scan()
        ticks = {}
        for p in procs.values():
            key = (p.pid, p.start)
            ticks[key] = p.ticks
            prev = self.prev_ticks.get(key)
            if prev is not None and jiffies > 0:
                p.cpu = 100.0 * (p.ticks - prev) / jiffies
        self.prev_ticks = ticks
        self.procs = procs
        self.mem = meminfo()
        self.last = time.monotonic()

    def build_rows(self):
        flt = self.filter.lower()
        ps = []
        for p in self.procs.values():
            if self.only_me and p.uid != ME:
                continue
            if not self.kthreads and p.kthread and not flt:
                continue
            if flt and flt not in p.cmd.lower() and flt not in p.name.lower() \
                    and flt != str(p.pid):
                continue
            ps.append(p)
        _, keyf, desc = SORTS[self.sort]
        desc ^= self.reverse
        ps.sort(key=keyf, reverse=desc)
        if not self.tree:
            self.rows = [(p, "") for p in ps]
            return
        present = {p.pid for p in ps}
        kids = {}
        roots = []
        for p in ps:  # порядок сортировки сохраняется внутри братьев
            if p.ppid in present and p.ppid != p.pid:
                kids.setdefault(p.ppid, []).append(p)
            else:
                roots.append(p)
        out = []

        def walk(p, pre, last, depth):
            out.append((p, "" if depth == 0 else pre + ("└─ " if last else "├─ ")))
            ch = kids.get(p.pid, [])
            npre = "" if depth == 0 else pre + ("   " if last else "│  ")
            for i, c in enumerate(ch):
                walk(c, npre, i == len(ch) - 1, depth + 1)

        for r in roots:
            walk(r, "", True, 0)
        self.rows = out

    def selected(self):
        if 0 <= self.sel_idx < len(self.rows):
            return self.rows[self.sel_idx][0]
        return None

    def say(self, text):
        self.msg = (text, time.monotonic() + 3)

    # рисование
    def put(self, y, x, s, attr=0):
        h, w = self.scr.getmaxyx()
        if y < 0 or y >= h or x >= w:
            return
        s = s[: max(0, w - x - (1 if y == h - 1 else 0))]
        try:
            self.scr.addstr(y, x, s, attr)
        except curses.error:
            pass

    def level(self, frac):
        return C.SAGE if frac < 0.6 else C.ACC if frac < 0.85 else C.PINK

    def bar(self, y, x, w, label, frac, right):
        frac = max(0.0, min(1.0, frac))
        self.put(y, x, label, C.ACC | curses.A_BOLD)
        bw = w - len(label) - len(right) - 2
        if bw < 3:
            return
        n = round(frac * bw)
        self.put(y, x + len(label) + 1, "━" * n, self.level(frac))
        self.put(y, x + len(label) + 1 + n, "─" * (bw - n), C.DIM)
        self.put(y, x + w - len(right), right, C.TEXT)

    def draw_header(self, w):
        up = float(read("/proc/uptime").split()[0] or 0)
        load = read("/proc/loadavg").split()[:3]
        y = 0
        self.put(y, 1, "✿ lavtop", C.ACC | curses.A_BOLD)
        info = f"  {self.host}  ·  аптайм {dur(up)}  ·  нагрузка {' '.join(load)}"
        self.put(y, 10, info, C.DIM)
        extra = []
        if self.temp_path:
            t = read(self.temp_path).strip()
            if t:
                extra.append((f"{int(t) / 1000:.0f}°C", int(t) > 80000))
        if self.bat_path:
            cap = read(self.bat_path + "/capacity").strip()
            st = read(self.bat_path + "/status").strip()
            if cap:
                sym = "⚡" if st == "Charging" else "▮"
                extra.append((f"{sym}{cap}%", int(cap) < 15 and st != "Charging"))
        x = w - 1
        for txt, warn in reversed(extra):
            x -= len(txt) + 2
            self.put(y, x, txt, C.PINK if warn else C.SAGE)
        y += 2

        cols = 4 if w >= 160 else 2 if w >= 70 else 1
        cw = (w - 2) // cols
        nrows = -(-NCPU // cols)
        for i, pct in enumerate(self.cores):
            r, c = i % nrows, i // nrows
            self.bar(y + r, 1 + c * cw, cw - 2, f"{i:>2}", pct / 100, f"{pct:5.1f}%")
        y += nrows + 1

        tot = self.mem.get("MemTotal", 1)
        used = tot - self.mem.get("MemAvailable", 0)
        stot = self.mem.get("SwapTotal", 0)
        sused = stot - self.mem.get("SwapFree", 0)
        half = (w - 2) // 2 if w >= 70 else w - 2
        self.bar(y, 1, half - 2, "ОЗУ ", used / tot, f"{human(used)}/{human(tot)}")
        sx, sy = (1 + half, y) if w >= 70 else (1, y + 1)
        self.bar(sy, sx, half - 2, "Своп", sused / stot if stot else 0,
                 f"{human(sused)}/{human(stot)}")
        y = sy + 1

        gw = w - 12
        h = self.hist[-gw:]
        self.put(y, 1, "CPU ", C.ACC | curses.A_BOLD)
        self.put(y, 5, f"{self.total:5.1f}%", C.TEXT)
        for i, v in enumerate(h):
            self.put(y, 12 + gw - len(h) + i, SPARK[min(7, int(v / 100 * 8))],
                     self.level(v / 100))
        return y + 2

    def columns(self, w):
        return [("PID", 7, ">"), ("USER", 9, "<"), ("NI", 3, ">"), ("S", 1, "<"),
                ("CPU%", 6, ">"), ("MEM%", 5, ">"), ("RSS", 7, ">"),
                ("TIME", 9, ">"), ("THR", 4, ">")]

    def draw_list(self, y, w, h):
        cols = self.columns(w)
        hdr_attr = C.HEAD
        self.put(y, 0, " " * w, hdr_attr)
        x = 1
        sort_title = SORTS[self.sort][0]
        for name, cw, al in cols + [("COMMAND", 0, "<")]:
            txt = f"{name:{al}{cw}}" if cw else name
            a = hdr_attr | (curses.A_BOLD | curses.A_UNDERLINE if name == sort_title else 0)
            self.put(y, x, txt, a)
            x += cw + 1
        y += 1
        self.list_y = y
        avail = max(1, h - y - 1)
        self.list_h = avail

        # выделение следует за PID
        if self.sel_pid is not None:
            for i, (p, _) in enumerate(self.rows):
                if p.pid == self.sel_pid:
                    self.sel_idx = i
                    break
        self.sel_idx = max(0, min(self.sel_idx, len(self.rows) - 1))
        if self.rows:
            self.sel_pid = self.rows[self.sel_idx][0].pid
        if self.sel_idx < self.top:
            self.top = self.sel_idx
        elif self.sel_idx >= self.top + avail:
            self.top = self.sel_idx - avail + 1
        self.top = max(0, min(self.top, max(0, len(self.rows) - avail)))

        tot = self.mem.get("MemTotal", 1)
        for i in range(avail):
            idx = self.top + i
            if idx >= len(self.rows):
                break
            p, branch = self.rows[idx]
            sel = idx == self.sel_idx
            base = C.SEL if sel else 0
            if sel:
                self.put(y + i, 0, " " * w, base)
            memp = 100 * p.rss / tot
            vals = [str(p.pid), p.user[:9], str(p.nice), p.state,
                    f"{p.cpu:.1f}", f"{memp:.1f}", human(p.rss) if p.rss else "0",
                    cputime(p.ticks), str(p.threads)]
            x = 1
            for (name, cw, al), v in zip(cols, vals):
                a = base or C.TEXT
                if not sel:
                    if name == "PID" or (name == "USER" and p.uid != ME):
                        a = C.DIM
                    elif name == "CPU%":
                        a = self.level(p.cpu / 100) if p.cpu >= 1 else C.DIM
                    elif name == "MEM%":
                        a = self.level(memp / 30) if memp >= 1 else C.DIM
                    elif name == "S":
                        a = C.SAGE if p.state == "R" else C.PINK if p.state in "DZT" else C.DIM
                    elif name == "NI" and p.nice:
                        a = C.PINK if p.nice < 0 else C.SAGE
                self.put(y + i, x, f"{v:{al}{cw}}", a)
                x += cw + 1
            if branch:
                self.put(y + i, x, branch, base or C.DIM)
                x += len(branch)
            cmd = p.cmd
            exe = cmd.split(" ", 1)[0]
            short = os.path.basename(exe) if exe.startswith("/") else exe
            rest = cmd[len(exe):]
            nattr = base | curses.A_BOLD if sel else (C.DIM if p.kthread else C.ACC)
            self.put(y + i, x, short, nattr)
            self.put(y + i, x + len(short), rest, base or C.DIM)

    def draw_footer(self, h, w):
        y = h - 1
        self.put(y, 0, " " * (w - 1), C.FOOT)
        if self.mode == "filter":
            self.put(y, 1, f"поиск: {self.filter}▏", C.FOOT | curses.A_BOLD)
            self.put(y, w - 28, "⏎ готово  Esc сбросить", C.FOOT)
            return
        text, until = self.msg
        if text and time.monotonic() < until:
            self.put(y, 1, text, C.FOOT | curses.A_BOLD)
            return
        keys = [("q", "выход"), ("/", "поиск"), ("c m p n", "сорт"), ("t", "дерево"),
                ("u", "мои"), ("k", "сигнал"), ("⏎", "инфо"), ("?", "помощь")]
        x = 1
        for k, d in keys:
            if x + len(k) + len(d) + 3 > w - 30:
                break
            self.put(y, x, k, C.FOOT | curses.A_BOLD)
            self.put(y, x + len(k) + 1, d, C.FOOT)
            x += len(k) + len(d) + 3
        state = []
        if self.filter:
            state.append(f"«{self.filter}»")
        if self.tree:
            state.append("дерево")
        if self.only_me:
            state.append("мои")
        if self.paused:
            state.append("пауза")
        state.append(f"{len(self.rows)} проц.")
        s = "  ".join(state)
        self.put(y, w - len(s) - 2, s, C.FOOT)

    def box(self, lines, title, width=None):
        h, w = self.scr.getmaxyx()
        bw = min(w - 4, width or max(len(title) + 6, *(len(l[0]) for l in lines)) + 4)
        bh = min(h - 2, len(lines) + 2)
        y0, x0 = (h - bh) // 2, (w - bw) // 2
        for i in range(bh):
            self.put(y0 + i, x0, " " * bw, C.BOX)
        self.put(y0, x0, "╭" + "─" * (bw - 2) + "╮", C.BOXB)
        self.put(y0 + bh - 1, x0, "╰" + "─" * (bw - 2) + "╯", C.BOXB)
        for i in range(1, bh - 1):
            self.put(y0 + i, x0, "│", C.BOXB)
            self.put(y0 + i, x0 + bw - 1, "│", C.BOXB)
        self.put(y0, x0 + 2, f" {title} ", C.BOXB | curses.A_BOLD)
        for i, (txt, attr) in enumerate(lines[: bh - 2]):
            self.put(y0 + 1 + i, x0 + 2, txt[: bw - 4], attr)

    def draw_details(self):
        p = self.procs.get(self.sel_pid)
        if not p:
            self.mode = "list"
            return
        d = f"/proc/{p.pid}"
        try:
            exe = os.readlink(d + "/exe")
        except OSError:
            exe = "—"
        try:
            cwd = os.readlink(d + "/cwd")
        except OSError:
            cwd = "—"
        try:
            fds = str(len(os.listdir(d + "/fd")))
        except OSError:
            fds = "—"
        io = {}
        for line in read(d + "/io").splitlines():
            k, _, v = line.partition(":")
            io[k] = int(v)
        up = float(read("/proc/uptime").split()[0] or 0)
        age = up - p.start / HZ
        par = self.procs.get(p.ppid)
        T, D, A = C.TEXT, C.DIM, C.ACC
        lines = [
            (f"{p.name}", A | curses.A_BOLD),
            ("", 0),
            (f"PID {p.pid}   родитель {p.ppid} ({par.name if par else '—'})", T),
            (f"пользователь {p.user}   состояние {STATES.get(p.state, p.state)}", T),
            (f"CPU {p.cpu:.1f}%   память {human(p.rss)}   потоков {p.threads}   nice {p.nice}", T),
            (f"запущен {dur(age)} назад   время CPU {cputime(p.ticks)}", T),
            (f"открытых файлов {fds}", T),
        ]
        if io:
            lines.append((f"диск: прочитано {human(io.get('read_bytes', 0))}"
                          f"   записано {human(io.get('write_bytes', 0))}", T))
        lines += [("", 0), (f"exe  {exe}", D), (f"cwd  {cwd}", D), ("", 0)]
        h, w = self.scr.getmaxyx()
        bw = min(w - 8, 100)
        cmd = p.cmd
        while cmd and len(lines) < h - 6:
            lines.append((cmd[: bw - 4], T))
            cmd = cmd[bw - 4:]
        lines += [("", 0), ("k сигнал   + − приоритет   Esc закрыть", D)]
        self.box(lines, "процесс", bw)

    def draw_kill(self):
        p = self.selected()
        if not p:
            self.mode = "list"
            return
        lines = [(f"{p.name}  (pid {p.pid})", C.ACC | curses.A_BOLD), ("", 0)]
        for k, n, _ in SIGNALS:
            hint = {"TERM": "вежливо попросить завершиться", "KILL": "убить немедленно",
                    "HUP": "перечитать конфиг / отключить", "INT": "как Ctrl+C",
                    "STOP": "заморозить", "CONT": "разморозить"}[n]
            lines.append((f" {k}  SIG{n:<5} {hint}", C.PINK if n == "KILL" else C.TEXT))
        lines += [("", 0), ("Esc отмена", C.DIM)]
        self.box(lines, "отправить сигнал", 46)

    def draw_help(self):
        L = [
            ("↑↓  PgUp PgDn  Home End   — перемещение", C.TEXT),
            ("мышь: клик — выбрать, колесо — прокрутка", C.TEXT),
            ("", 0),
            ("c  сортировать по CPU      m  по памяти", C.TEXT),
            ("p  по PID                  n  по имени", C.TEXT),
            ("T  по времени CPU          r  обратный порядок", C.TEXT),
            ("t  дерево процессов        u  только мои", C.TEXT),
            ("x  показать потоки ядра    /  поиск (Esc — сброс)", C.TEXT),
            ("", 0),
            ("⏎  подробности о процессе", C.TEXT),
            ("k  отправить сигнал (TERM, KILL, STOP…)", C.TEXT),
            ("+ −  изменить приоритет (nice)", C.TEXT),
            ("пробел  пауза          < >  частота обновления", C.TEXT),
            ("q  выход", C.TEXT),
        ]
        self.box(L, "помощь", 58)

    def draw(self):
        self.scr.erase()
        h, w = self.scr.getmaxyx()
        if h < 12 or w < 40:
            self.put(0, 0, "окно слишком маленькое", C.PINK)
            self.scr.refresh()
            return
        self.build_rows()
        y = self.draw_header(w)
        self.draw_list(y, w, h)
        self.draw_footer(h, w)
        if self.mode == "details":
            self.draw_details()
        elif self.mode == "kill":
            self.draw_kill()
        elif self.mode == "help":
            self.draw_help()
        self.scr.refresh()

    # действия
    def move(self, delta):
        self.sel_pid = None
        self.sel_idx = max(0, min(len(self.rows) - 1, self.sel_idx + delta))
        if self.rows:
            self.sel_pid = self.rows[self.sel_idx][0].pid

    def send(self, sig, name):
        p = self.procs.get(self.sel_pid)
        if not p:
            return
        try:
            os.kill(p.pid, sig)
            self.say(f"SIG{name} → {p.name} ({p.pid})")
        except PermissionError:
            self.say(f"нет прав на {p.name} ({p.pid}) — нужен root")
        except ProcessLookupError:
            self.say("процесс уже завершился")

    def renice(self, delta):
        p = self.procs.get(self.sel_pid)
        if not p:
            return
        new = max(-20, min(19, p.nice + delta))
        try:
            os.setpriority(os.PRIO_PROCESS, p.pid, new)
            p.nice = new
            self.say(f"nice {p.name} = {new}")
        except PermissionError:
            self.say("понизить nice может только root" if delta < 0 else "нет прав")
        except ProcessLookupError:
            self.say("процесс уже завершился")

    def key(self, ch):
        if self.mode == "filter":
            if ch in ("\n", "\r", curses.KEY_ENTER):
                self.mode = "list"
            elif ch == "\x1b":
                self.filter, self.mode = "", "list"
            elif ch in (curses.KEY_BACKSPACE, "\x7f", "\b"):
                self.filter = self.filter[:-1]
            elif isinstance(ch, str) and ch.isprintable():
                self.filter += ch
            self.sel_idx = 0
            self.sel_pid = None
            return True
        if self.mode == "kill":
            for k, n, sig in SIGNALS:
                if ch == k:
                    self.send(sig, n)
            self.mode = "list"
            return True
        if self.mode in ("details", "help"):
            if ch in ("\x1b", "q", "\n", "\r", "?", curses.KEY_ENTER):
                self.mode = "list"
                return True
            if self.mode == "help":
                self.mode = "list"
                return True
            # в подробностях работают k и +/-
        if ch == "q":
            return False
        page = getattr(self, "list_h", 10)
        moves = {curses.KEY_UP: -1, curses.KEY_DOWN: 1,
                 curses.KEY_PPAGE: -page, curses.KEY_NPAGE: page,
                 curses.KEY_HOME: -10 ** 9, curses.KEY_END: 10 ** 9, "g": -10 ** 9,
                 "G": 10 ** 9}
        if ch in moves:
            self.move(moves[ch])
        elif ch == "\x1b":
            self.filter = ""
        elif ch == "/":
            self.mode = "filter"
        elif ch in ("c", "m", "p", "n", "T"):
            new = {"c": "cpu", "m": "mem", "p": "pid", "n": "name", "T": "time"}[ch]
            self.reverse = False if new != self.sort else self.reverse
            self.sort = new
        elif ch == "r":
            self.reverse = not self.reverse
        elif ch == "t":
            self.tree = not self.tree
        elif ch == "u":
            self.only_me = not self.only_me
        elif ch == "x":
            self.kthreads = not self.kthreads
            self.say("потоки ядра: " + ("показаны" if self.kthreads else "скрыты"))
        elif ch == "k" or ch == curses.KEY_DC:
            if self.selected():
                self.mode = "kill"
        elif ch in ("\n", "\r", curses.KEY_ENTER):
            if self.selected():
                self.mode = "details"
        elif ch in ("+", "="):
            self.renice(+1)
        elif ch in ("-", "_"):
            self.renice(-1)
        elif ch == " ":
            self.paused = not self.paused
        elif ch in ("<", ">"):
            global INTERVAL
            INTERVAL = max(0.5, min(10, INTERVAL + (0.5 if ch == ">" else -0.5)))
            self.say(f"обновление каждые {INTERVAL:.1f} с")
        elif ch == "?":
            self.mode = "help"
        elif ch == curses.KEY_MOUSE:
            self.mouse()
        return True

    def mouse(self):
        try:
            _, x, y, _, b = curses.getmouse()
        except curses.error:
            return
        if b & curses.BUTTON4_PRESSED:
            self.move(-3)
        elif b & getattr(curses, "BUTTON5_PRESSED", 0x200000):
            self.move(3)
        elif b & (curses.BUTTON1_CLICKED | curses.BUTTON1_PRESSED |
                  curses.BUTTON1_DOUBLE_CLICKED):
            if self.mode != "list":
                self.mode = "list"
                return
            idx = self.top + y - self.list_y
            if y >= self.list_y and 0 <= idx < len(self.rows):
                self.sel_idx = idx
                self.sel_pid = self.rows[idx][0].pid
                if b & curses.BUTTON1_DOUBLE_CLICKED:
                    self.mode = "details"

    def run(self):
        while True:
            if not self.paused and time.monotonic() - self.last >= INTERVAL:
                self.sample()
            self.draw()
            wait = max(0.05, INTERVAL - (time.monotonic() - self.last))
            self.scr.timeout(int(wait * 1000))
            try:
                ch = self.scr.get_wch()
            except curses.error:
                continue
            if ch == curses.KEY_RESIZE:
                continue
            if not self.key(ch):
                return


class C:
    pass


def init_colors():
    curses.start_color()
    curses.use_default_colors()
    rich = curses.COLORS >= 256
    acc = 183 if rich else curses.COLOR_MAGENTA     # #d7afff ≈ лаванда #bba7e3
    sage = 151 if rich else curses.COLOR_GREEN      # #afd7af ≈ шалфей #a7cdb6
    pink = 217 if rich else curses.COLOR_RED        # #ffafaf мягкий розовый
    dim = 244 if rich else curses.COLOR_WHITE
    text = 254 if rich else -1
    selbg = 60 if rich else curses.COLOR_BLUE       # #5f5f87
    headbg = 237 if rich else curses.COLOR_BLACK
    boxbg = 235 if rich else curses.COLOR_BLACK
    pairs = [("ACC", acc, -1), ("SAGE", sage, -1), ("PINK", pink, -1), ("DIM", dim, -1),
             ("TEXT", text, -1), ("SEL", 255 if rich else curses.COLOR_WHITE, selbg),
             ("HEAD", acc, headbg), ("FOOT", dim, headbg), ("BOX", text, boxbg),
             ("BOXB", acc, boxbg)]
    for i, (name, fg, bg) in enumerate(pairs, 1):
        curses.init_pair(i, fg, bg)
        setattr(C, name, curses.color_pair(i))
    if not rich:
        C.DIM |= curses.A_DIM


def main(scr):
    curses.curs_set(0)
    curses.set_escdelay(25)
    scr.keypad(True)
    curses.mousemask(curses.ALL_MOUSE_EVENTS)
    curses.mouseinterval(200)
    init_colors()
    App(scr).run()


if __name__ == "__main__":
    locale.setlocale(locale.LC_ALL, "")
    try:
        curses.wrapper(main)
    except KeyboardInterrupt:
        pass
