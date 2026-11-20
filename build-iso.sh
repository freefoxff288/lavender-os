#!/bin/sh
# Собирает установочный live-ISO Lavender OS через void-mklive.
# Нужен root и пакеты: xorriso squashfs-tools. Запуск: sudo sh build-iso.sh
# Результат: build/lavender-os-ГГГГММДД.iso
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
B=$HERE/build
REPO=https://repo-default.voidlinux.org/current
[ "$(id -u)" = 0 ] || { echo "нужен root: sudo sh $0"; exit 1; }
[ -d "$HERE/overlay" ] || { echo "нет overlay/ — сначала: sh build-overlay.sh (без sudo)"; exit 1; }
[ -d "$B/void-mklive" ] || git clone --depth 1 https://github.com/void-linux/void-mklive "$B/void-mklive"

# --- что кладём в live-систему ---
INC=$B/include
rm -rf "$INC"; mkdir -p "$INC/opt/lavender-os" "$INC/usr/local/bin" "$INC/usr/local/libexec" "$INC/etc/profile.d"
# графический установщик (GTK4) — собирается на этой же системе
make -s -C "$HERE/installer" lavender-installer
install -m755 "$HERE/installer/lavender-installer" "$INC/usr/local/libexec/lavender-installer"
cat > "$INC/usr/local/bin/lavender-installer" <<'E'
#!/bin/sh
# Графический установщик в киоск-композиторе cage (программная отрисовка — работает на любой видеокарте)
[ "$(id -u)" = 0 ] || exec sudo "$0" "$@"
export XDG_RUNTIME_DIR=/run/lavender-xdg WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1 \
       GSK_RENDERER=cairo GTK_A11Y=none XKB_DEFAULT_LAYOUT=us,ru XKB_DEFAULT_OPTIONS=grp:ctrl_shift_toggle LANG=ru_RU.UTF-8
mkdir -p -m700 "$XDG_RUNTIME_DIR"
pkill -x cage; pkill -x seatd; sleep 0.3; rm -f /run/seatd.sock   # остатки прошлого запуска (в live больше никто seatd не использует)
# seatd-launch даёт cage доступ к экрану и клавиатуре (в libseat Void нет встроенного seat)
seatd-launch -- cage -s -- /usr/local/libexec/lavender-installer "$@" 2>/tmp/lavender-installer.log
E
chmod 755 "$INC/usr/local/bin/lavender-installer"
cp -a "$HERE/install.sh" "$HERE/README.md" "$HERE/packages" "$HERE/keys" "$HERE/repos" "$HERE/overlay" "$INC/opt/lavender-os/"
cat > "$INC/usr/local/bin/lavender-install" <<'E'
#!/bin/sh
[ "$(id -u)" = 0 ] || exec sudo "$0" "$@"
exec sh /opt/lavender-os/install.sh "$@"
E
cat > "$INC/etc/profile.d/zz-lavender.sh" <<'E'
if [ "$(tty)" = /dev/tty1 ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
  # графический установщик запускается сам один раз; если его закрыть — остаётся консоль
  if [ ! -e /run/lavender-gui-started ]; then
    sudo touch /run/lavender-gui-started; lavender-installer
  fi
  printf '\n  \033[1;35mLavender OS\033[0m — установка\n  Графический установщик:  \033[1mlavender-installer\033[0m\n  Текстовый (стирает весь диск):  lavender-install\n  Wi-Fi: nmtui    Инструкция: less /opt/lavender-os/README.md\n\n'
fi
E
cat > "$INC/etc/issue" <<'E'

  Lavender OS — установочная live-система (основа: Void Linux)

  Вход: anon / voidlinux (sudo без пароля), root / voidlinux
  Установка:  lavender-installer   (текстовый: lavender-install)
  Wi-Fi:      nmtui

E
chmod 755 "$INC/usr/local/bin/lavender-install"
chown -R root:root "$INC"

# после установки пакетов: кириллический шрифт консоли (иначе вместо русского — квадраты)
cat > "$B/postsetup.sh" <<'E'
#!/bin/sh
printf 'FONT=cyr-sun16\n' >> "$1/etc/rc.conf"
E
chmod 755 "$B/postsetup.sh"

OUT=$B/lavender-os-$(date +%Y%m%d).iso
cd "$B/void-mklive"
./mklive.sh -a x86_64 -r "$REPO" -c "$B/xbps-cache" \
  -T "Lavender OS" -k us -l ru_RU.UTF-8 \
  -p "newt dosfstools parted curl NetworkManager cage seatd gtk4 json-glib font-inter dejavu-fonts-ttf ntfs-3g efibootmgr" \
  -S "dbus NetworkManager" -C "live.autologin" \
  -I "$INC" -x "$B/postsetup.sh" -o "$OUT"
chown "${SUDO_UID:-0}:${SUDO_GID:-0}" "$OUT"
echo "ISO: $OUT ($(du -h "$OUT" | cut -f1))"
