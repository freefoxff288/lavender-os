#!/usr/bin/env python3
"""Управление тестовой VM через монитор QEMU.
  vmctl.py type 'текст\\n'   — набрать (латиница/цифры/символы; \\n = Enter)
  vmctl.py key ret|tab|esc|ctrl-alt-f2 ...
  vmctl.py shot файл.png     — снимок экрана
  vmctl.py cmd 'system_powerdown'"""
import socket, sys, time, os, subprocess
SOCK = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'vm', 'monitor.sock')
SPECIAL = {' ': 'spc', '\n': 'ret', '-': 'minus', '=': 'equal', '/': 'slash', '.': 'dot', ',': 'comma',
           ';': 'semicolon', "'": 'apostrophe', '`': 'grave_accent', '\\': 'backslash', '[': 'bracket_left',
           ']': 'bracket_right', '\t': 'tab'}
SHIFT = {'_': 'minus', '+': 'equal', '?': 'slash', '>': 'dot', '<': 'comma', ':': 'semicolon', '"': 'apostrophe',
         '~': 'grave_accent', '|': 'backslash', '{': 'bracket_left', '}': 'bracket_right', '!': '1', '@': '2',
         '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0'}
def mon(*cmds):
    s = socket.socket(socket.AF_UNIX); s.connect(SOCK); s.settimeout(2)
    def drain():
        time.sleep(0.05); out = b''
        try:
            while True:
                d = s.recv(65536)
                if not d: break
                out += d
                if out.endswith(b'(qemu) '): break
        except socket.timeout: pass
        return out
    drain()
    for c in cmds: s.sendall((c + '\n').encode()); drain()
    s.close()
def key(ch):
    if ch in SPECIAL: return SPECIAL[ch]
    if ch in SHIFT: return 'shift-' + SHIFT[ch]
    if ch.isupper(): return 'shift-' + ch.lower()
    return ch
a = sys.argv[1:]
if a[0] == 'type': mon(*['sendkey ' + key(c) for c in a[1].encode().decode('unicode_escape')])
elif a[0] == 'key': mon(*['sendkey ' + k for k in a[1:]])
elif a[0] == 'cmd': mon(*a[1:])
elif a[0] == 'shot':
    ppm = a[1] + '.ppm'; mon('screendump ' + os.path.abspath(ppm)); time.sleep(0.5)
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', ppm, a[1]], check=True); os.remove(ppm)
