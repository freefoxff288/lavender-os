#!/bin/sh
# Исправление для флешки Lavender OS 2026.11.20 прямо в live-системе (запускать от root):
#   curl -L github.com/freefoxff288/lavender-os/raw/main/fix.sh | sh
# Переключает репозиторий noctalia на копию в GitHub (pages.dev и SourceForge бывают заблокированы)
# и заново открывает графический установщик. На дисках ничего не меняет.
NEW=https://github.com/freefoxff288/lavender-os/releases/download/pkgs
[ "$(id -u)" = 0 ] || { echo "Нужен root: войди как root (пароль voidlinux) и запусти ещё раз"; exit 1; }
F="/opt/lavender-os/install.sh /opt/lavender-os/repos/10-universalrepository.conf"
sed -i "s|https://universalrepo.pages.dev/void|$NEW|; s|https://downloads.sourceforge.net/project/universalrepository/void|$NEW|" $F
echo "Проверяю доступ к пакетам на GitHub..."
if curl -sSL --max-time 40 -o /dev/null "$NEW/x86_64-repodata"; then
  echo "OK: пакеты доступны."
else
  echo "GitHub не открывается — напиши об этом. Установщик не запускаю."; exit 1
fi
# перенести сетевые настройки live (свой шлюз/DNS) в установленную систему
grep -q system-connections /opt/lavender-os/install.sh ||
  sed -i 's|^cp /etc/resolv.conf "$T/etc/"$|&\nmkdir -p "$T/etc/NetworkManager/system-connections"; cp -a /etc/NetworkManager/system-connections/. "$T/etc/NetworkManager/system-connections/" 2>/dev/null \|\| true|' /opt/lavender-os/install.sh
grep -q "$NEW" /opt/lavender-os/install.sh || { echo "Не удалось поправить install.sh"; exit 1; }
echo "Запускаю установщик (через 3 секунды)..."
sleep 3
exec lavender-installer
