#!/bin/sh
# Лаванда: всё, что требует root.
#   1) библиотека gtk4-layer-shell для заметок (dropnotes)
#   2) экран входа lavender-greeter вместо noctalia-greeter (greetd + cage)
#   3) удаление rEFInd (система грузится через GRUB)
# Запуск:  sudo sh ~/Projects/lavender-greeter/install.sh
# Откат экрана входа к noctalia-greeter:  sudo sh ~/Projects/lavender-greeter/install.sh --remove
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
[ "$(id -u)" = 0 ] || { echo "нужен root: sudo sh $0"; exit 1; }
CONF=/etc/greetd/config.toml
BACKUP=/etc/greetd/config.toml.noctalia-greeter

if [ "$1" = "--remove" ]; then
    [ -f "$BACKUP" ] && cp "$BACKUP" "$CONF" && echo "Вернул noctalia-greeter. Применится после перезагрузки."
    exit 0
fi

echo "== 1. gtk4-layer-shell"
xbps-install -Sy gtk4-layer-shell gtk4-layer-shell-devel cage

echo "== 2. lavender-greeter"
[ -x "$HERE/lavender-greeter" ] || { echo "нет собранного $HERE/lavender-greeter — сначала make"; exit 1; }
install -Dm755 "$HERE/lavender-greeter" /usr/local/bin/lavender-greeter
install -Dm644 "$HERE/wallpaper.png" /etc/lavender-greeter/wallpaper.png
mkdir -p /etc/lavender-greeter/avatars
[ -f /etc/lavender-greeter/config.ini ] || printf '[greeter]\nwallpaper=/etc/lavender-greeter/wallpaper.png\n' > /etc/lavender-greeter/config.ini
install -d -o _greeter -g _greeter /var/cache/lavender-greeter
[ -f /var/cache/lavender-greeter/last ] || { echo d3r > /var/cache/lavender-greeter/last; chown _greeter: /var/cache/lavender-greeter/last; }
[ -f "$BACKUP" ] || cp "$CONF" "$BACKUP"
cat > "$CONF" <<'EOF'
# Лаванда: экран входа lavender-greeter. Откат: sudo sh ~/Projects/lavender-greeter/install.sh --remove
# tty1 остаётся текстовым запасным входом (Ctrl+Alt+F1)
[terminal]
vt = 7

[default_session]
command = "env XKB_DEFAULT_LAYOUT=us,ru XKB_DEFAULT_OPTIONS=grp:ctrl_shift_toggle cage -s -d -- /usr/local/bin/lavender-greeter"
user = "_greeter"
EOF

echo "== 3. удаление rEFInd"
for n in $(efibootmgr | awk '/rEFInd/ { sub(/^Boot/, "", $1); sub(/\*$/, "", $1); print $1 }'); do
    efibootmgr -q -b "$n" -B && echo "убрал загрузочную запись Boot$n (rEFInd)"
done
rm -rf /boot/efi/EFI/refind
rm -f /boot/refind_linux.conf
rmdir /boot/efi/EFI/tools 2>/dev/null || true
xbps-query refind >/dev/null 2>&1 && xbps-remove -y refind
echo
efibootmgr | head -4
echo
echo "Готово. Экран входа сменится после перезагрузки."
echo "Если вход не работает: Ctrl+Alt+F1 → вход в консоли → sudo sh $0 --remove"
