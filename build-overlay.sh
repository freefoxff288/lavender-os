#!/bin/sh
# Собирает overlay/ из текущего рабочего стола: конфиги → /etc/skel, обои, экран входа, dconf.
# Личное (история, сессии, закладки, прокси, .bak) и проприетарное не копируется.
# Пути к домашней папке заменяются на @HOME@ — установщик подставит папку нового пользователя.
# Запуск (без root): sh build-overlay.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HOME
O=$HERE/overlay
S=$O/etc/skel
rm -rf "$O"; mkdir -p "$S/.config" "$S/.local/share" "$S/.local/state"

# --- ~/.config: только оформление ---
for d in niri noctalia gtk-3.0 gtk-4.0 qt5ct qt6ct foot alacritty fontconfig btop cava \
         fastfetch imv mpv yazi zathura Thunar xfce4 nvim; do
  [ -d "$SRC/.config/$d" ] && cp -a "$SRC/.config/$d" "$S/.config/"
done
mkdir -p "$S/.config/qutebrowser"
for f in config.py noctalia; do cp -a "$SRC/.config/qutebrowser/$f" "$S/.config/qutebrowser/"; done
cp -a "$SRC/.config/mimeapps.list" "$S/.config/"
cp -a "$SRC/.zshrc" "$S/"

# --- noctalia: настройки и свой плагин pill ---
mkdir -p "$S/.local/share/noctalia" "$S/.local/state/noctalia"
cp -a "$SRC/.local/share/noctalia/plugins" "$S/.local/share/noctalia/"
# вся папка состояния: настройки, плагины, шаблоны тем, отметка .setup-complete (иначе мастер первого запуска)
cp -a "$SRC/.local/state/noctalia/." "$S/.local/state/noctalia/"
rm -f "$S/.local/state/noctalia/usage_counts.json"
sed -i 's/^\(applied_community_ids = \).*/\1"qutebrowser telegram thunar yazi"/' "$S/.local/state/noctalia/state.toml"

# --- neovim: готовые плагины и парсеры подсветки (языковые серверы Mason докачает при первом запуске) ---
mkdir -p "$S/.local/share/nvim"
cp -a "$SRC/.local/share/nvim/lazy" "$SRC/.local/share/nvim/site" "$S/.local/share/nvim/"

# --- облегчённый индекс иконок и курсор по умолчанию ---
mkdir -p "$S/.local/share/icons" "$S/.icons/default"
cp -a "$SRC/.local/share/icons/Papirus-Dark" "$S/.local/share/icons/"
cp -a "$SRC/.icons/default/index.theme" "$S/.icons/default/"

# --- чистка: резервные копии, мусор, кэш ---
find "$S" \( -name '*.bak*' -o -name '__pycache__' -o -name 'recently_used.json' \) -prune -exec rm -rf {} +
rm -f "$S/.config/niri/d3r-look.kdl" "$S"/.config/fastfetch/void-logo-green.* "$S/.config/noctalia/palettes/VoidGreen.json"

# --- правки под чистую систему ---
N=$S/.config/niri/config.kdl
sed -i '/d3r-files-fm1/d' "$N"                                    # D3R Files не входит
sed -i "s|$SRC/.local/bin/dropnotes|/usr/local/bin/dropnotes|g" "$N"   # заметки: Mod+N, живут в фоне
sed -i '/^\/\/ d3r: TLauncher/,/^}/d; /^\/\/ d3r: окно выбора файла/,/^}/d' "$N"
sed -i 's|spawn "[^"]*/d3r-files";|spawn "thunar";|' "$N"           # Mod+E → Thunar
sed -i 's|^inode/directory=.*|inode/directory=thunar.desktop|' "$S/.config/mimeapps.list"
# D3R Code в систему не входит: текст и код открывает neovim
sed -i 's/d3r-code\.desktop;\?//g; /=$/d' "$S/.config/mimeapps.list"
# свои программы ставятся в /usr/local/bin: lavwall, lavshot, lavtop
sed -i 's|\$HOME/\.local/bin/lav|/usr/local/bin/lav|g' "$N" "$S/.local/share/noctalia/plugins/lavwall/shortcut.luau"
# fastfetch: строка батареи только если батарея есть (на ПК было «No such file or directory»)
sed -i 's|do c=\$(cat \$d/capacity)|do [ -e \$d ] \|\| continue; c=\$(cat \$d/capacity)|' "$S/.config/fastfetch/config.jsonc"
T=$S/.local/state/noctalia/settings.toml
sed -i 's|^\(\s*pinned = \).*|\1[ "foot", "firefox", "org.telegram.desktop", "thunar" ]|' "$T"
sed -i "s|$SRC/Pictures/Wallpapers/|/usr/share/backgrounds/lavender/|g; s|$SRC/.local/bin/devjoke|/usr/local/bin/devjoke|g" "$T"
grep -rlI "$SRC" "$S" | xargs -r sed -i "s|$SRC|@HOME@|g"
# личные пометки «d3r:» в комментариях (дистрибутив без ника автора) и git-кэш плагинов noctalia
grep -rlIw 'd3r' "$S" --exclude-dir=nvim | xargs -r sed -i -E 's/(\/\/|#|<!--) ?d3r: /\1 /; s/ — d3r \/ voidbook//; s/ \(d3r\)//; /include "d3r-look\.kdl"/d; s/вернуть include "d3r-look\.kdl"/вернуть прежний файл оформления/'
rm -rf "$S/.local/state/noctalia/plugins/sources"

# --- системные файлы ---
install -Dm644 -t "$O/usr/share/backgrounds/lavender" "$SRC"/Pictures/Wallpapers/lavender-*.png
# и все обои, на которые ссылаются настройки noctalia (например, сгенерированные lavwall)
grep -o '/usr/share/backgrounds/lavender/[^"]*' "$T" | sort -u | while read -r w; do
  f=$SRC/Pictures/Wallpapers/${w#/usr/share/backgrounds/lavender/}
  if [ -f "$f" ]; then install -Dm644 "$f" "$O$w"; else echo "ВНИМАНИЕ: нет обоев $f" >&2; fi
done
# свои программы — исходники в apps/, собираются здесь же
A=$HERE/apps
install -Dm755 "$A/devjoke/devjoke" "$O/usr/local/bin/devjoke"
make -s -C "$A/dropnotes" dropnotes
install -Dm755 "$A/dropnotes/dropnotes" "$O/usr/local/bin/dropnotes"

# обои (lavwall), красивые скриншоты (lavshot) и диспетчер задач (lavtop)
W=$A/lavender-wallpaper
make -s -C "$W" all
install -Dm755 "$W/lavwall" "$O/usr/local/bin/lavwall"
install -Dm755 "$W/lavshot" "$O/usr/local/bin/lavshot"
install -Dm644 "$W/lavwall.desktop" "$O/usr/local/share/applications/lavwall.desktop"
install -Dm644 "$W/lavshot.desktop" "$O/usr/local/share/applications/lavshot.desktop"
install -Dm755 "$A/lavtop/lavtop.py" "$O/usr/local/bin/lavtop"
install -Dm644 "$SRC/.local/share/applications/nvim-lavender.desktop" "$O/usr/local/share/applications/nvim-lavender.desktop"
install -Dm644 /dev/stdin "$O/usr/local/share/applications/lavtop.desktop" <<'E'
[Desktop Entry]
Type=Application
Name=Диспетчер задач
GenericName=Процессы
Comment=Процессы, нагрузка и память в терминале
Exec=foot --app-id=lavtop --title=lavtop lavtop
Icon=utilities-system-monitor
Terminal=false
Categories=System;Monitor;
Keywords=top;task;процессы;диспетчер;
E
G=$A/lavender-greeter
make -s -C "$G" lavender-greeter
install -Dm755 "$G/lavender-greeter" "$O/usr/local/bin/lavender-greeter"
install -Dm644 "$G/wallpaper.png" "$O/etc/lavender-greeter/wallpaper.png"
printf '[greeter]\nwallpaper=/etc/lavender-greeter/wallpaper.png\n' > "$O/etc/lavender-greeter/config.ini"
mkdir -p "$O/etc/lavender-greeter/avatars"

install -Dm755 /dev/stdin "$O/usr/local/bin/lavender-session" <<'E'
#!/bin/sh
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=niri XDG_SESSION_DESKTOP=niri
exec dbus-run-session niri
E

# GTK-настройки по умолчанию для всех (без прокси)
install -Dm644 /dev/stdin "$O/etc/dconf/profile/user" <<'E'
user-db:user
system-db:local
E
install -Dm644 /dev/stdin "$O/etc/dconf/db/local.d/00-lavender" <<'E'
[org/gnome/desktop/interface]
color-scheme='prefer-dark'
cursor-size=22
cursor-theme='Bibata-Modern-Classic'
document-font-name='Inter 10'
font-name='Inter 10'
gtk-theme='adw-gtk3-dark'
icon-theme='Papirus-Dark'
monospace-font-name='Fira Code 10'
E

# --- проверка: не утекло ли личное ---
if grep -rIlE "$SRC|10809|\.local/bin/(d3r|dropnotes|lav)" "$S"; then
  echo "ВНИМАНИЕ: выше файлы с личными ссылками — проверь вручную" >&2
fi
echo "overlay готов: $(du -sh "$O" | cut -f1)"
