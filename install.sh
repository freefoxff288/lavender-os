#!/bin/sh
# Lavender OS — установщик. Запускать от root с live-ISO (UEFI, нужен интернет).
# Обычно его запускает графический установщик (lavender-installer) в режиме LAVENDER_AUTO;
# без него задаёт вопросы в whiptail и ставит на весь выбранный диск.
#
# Способы разметки (MODE):
#   disk — стереть весь диск DISK: GPT, 512M EFI + ext4
#   free — в самую большую свободную область диска DISK; существующие разделы не трогаются
#   part — отформатировать раздел PART в ext4
# EFI: ESP=/dev/... — переиспользовать существующий EFI-раздел (без форматирования, например от Windows);
#      пусто — создать свой 512M (в режимах disk/free).
# Загрузчик — GRUB; пункт «Windows» добавляет /etc/grub.d/35_windows (ищет bootmgfw.efi на EFI-разделах).
set -eu
exec 3>&1   # fd 3 — экран/графический установщик; позже stdout уходит в лог

HERE=$(cd "$(dirname "$0")" && pwd)
T=/mnt/lavender
REPO=https://repo-default.voidlinux.org/current
# noctalia, adw-gtk-theme. Прямой адрес SourceForge: зеркало на pages.dev (Cloudflare) бывает заблокировано
EXTRA_REPO=https://downloads.sourceforge.net/project/universalrepository/void
LOG=/tmp/lavender-install.log
STEPS=6; N=0

die() {
  echo "@@ERROR $1" >&3
  [ -n "${LAVENDER_AUTO:-}" ] || whiptail --title "Ошибка" --msgbox "$1" 10 60 2>/dev/null || echo "Ошибка: $1" >&2
  exit 1
}
ask() { whiptail --title "Lavender OS" "$@" 3>&1 1>&2 2>&3; }
pkgs() { grep -v '^\s*#' "$HERE/packages/$1" | grep -v '^\s*$'; }
# секция из optional.txt: строки после "# [name]" до следующего "# ["
section() { awk -v s="# [$1]" '$0==s{f=1;next} /^# \[/{f=0} f&&NF' "$HERE/packages/optional.txt"; }
disk_of() { d=$(lsblk -no PKNAME "$1" 2>/dev/null | head -1); [ -n "$d" ] && echo "/dev/$d" || echo "$1"; }
# самая большая свободная область: «начало число_секторов»
largest_free() { sfdisk -F -q "$1" | awk 'NR>1 && $3>m {m=$3; s=$1} END{if (m) print s, m}'; }
# разделы, похожие на Windows (NTFS, BitLocker, служебные разделы Microsoft)
win_parts() {
  lsblk -nrpo NAME,TYPE,FSTYPE,PARTTYPE "$1" | awk '$2=="part" && ($3=="ntfs" || $3=="BitLocker" ||
    tolower($4) ~ /^(ebd0a0a2-b9e5-4433-87c0-68b6b72699c7|e3c9e316-0b5c-4db8-817d-f92df00215ae|de94bba4-06d1-4d40-a16a-bfd50179d6ac)$/) {print $1}'
}
# диск, с которого загружена live-система (его трогать нельзя)
live_disk() {
  src=$(findmnt -no SOURCE /run/initramfs/live 2>/dev/null || true)
  [ -n "$src" ] && disk_of "$src" || true
}

[ "$(id -u)" = 0 ] || die "Нужен root: sudo sh install.sh"
[ -d /sys/firmware/efi ] || die "Компьютер загружен не в режиме UEFI. Перезагрузись и выбери флешку с пометкой UEFI."
[ -n "${LAVENDER_AUTO:-}" ] || command -v whiptail >/dev/null || xbps-install -Sy newt >/dev/null || die "Нет whiptail (пакет newt)."
curl -sI "$REPO/" >/dev/null 2>&1 || die "Нет интернета. Подключись к сети и попробуй снова."
# до разметки: доступен ли репозиторий с noctalia (иначе установка упадёт уже после стирания диска)
curl -sSL --max-time 40 -o /dev/null "$EXTRA_REPO/x86_64-repodata" 2>/dev/null ||
  die "Не открывается репозиторий с noctalia ($EXTRA_REPO). Диски не тронуты. Проверь интернет или попробуй позже."

# ---------- вопросы ----------
# Без вопросов: LAVENDER_AUTO=файл с переменными
#   MODE DISK PART ESP WIPE_OK HOST USERN PW1 TZ OPT GPU
#   WIPE_OK должен совпадать с DISK, чтобы стереть диск, на котором есть Windows.
if [ -n "${LAVENDER_AUTO:-}" ]; then
  . "$LAVENDER_AUTO"
  rm -f "$LAVENDER_AUTO"   # там пароль
  MODE=${MODE:-disk}; PART=${PART:-}; ESP=${ESP:-}; WIPE_OK=${WIPE_OK:-}; OPT=${OPT:-}; GPU=${GPU:-none}
else
MODE=disk; PART=; ESP=
ask --msgbox "Добро пожаловать в установщик Lavender OS.\n\nОснова — Void Linux, рабочий стол niri + noctalia.\nВыбранный диск будет ПОЛНОСТЬЮ стёрт.\n\nЧтобы поставить рядом с Windows, используй графический установщик." 13 64

LIVE=$(live_disk)
set --
for d in $(lsblk -dno NAME,TYPE | awk '$2=="disk"{print $1}'); do
  [ "/dev/$d" = "$LIVE" ] && continue
  note=""; [ -n "$(win_parts "/dev/$d")" ] && note="  [WINDOWS!]"
  set -- "$@" "/dev/$d" "$(lsblk -dno SIZE,MODEL "/dev/$d" | xargs)$note"
done
[ $# -gt 0 ] || die "Диски не найдены."
DISK=$(ask --menu "Диск для установки (будет стёрт целиком):" 15 70 6 "$@") || exit 1
WIPE_OK=$DISK

HOST=$(ask --inputbox "Имя компьютера:" 8 50 lavender) || exit 1
USERN=$(ask --inputbox "Имя пользователя (латиница, строчные):" 8 50) || exit 1
while :; do
  PW1=$(ask --passwordbox "Пароль для $USERN (он же для sudo):" 8 50) || exit 1
  PW2=$(ask --passwordbox "Повтори пароль:" 8 50) || exit 1
  [ -n "$PW1" ] && [ "$PW1" = "$PW2" ] && break
  ask --msgbox "Пароли пустые или не совпадают." 7 40
done
TZ=$(ask --inputbox "Часовой пояс (как в /usr/share/zoneinfo):" 8 50 Europe/Moscow) || exit 1
OPT=$(ask --checklist "Дополнительные наборы:" 13 70 3 \
  media "OBS, ffmpeg, cava, yt-dlp, qBittorrent" ON \
  dev   "компиляторы, cmake, nodejs" OFF \
  fun   "cbonsai, tty-clock, genact, sl" OFF) || exit 1

# видеокарта NVIDIA: vendor 0x10de, класс 0x03xxxx (дисплейный контроллер)
GPU=none
for d in /sys/bus/pci/devices/*; do
  [ "$(cat "$d/vendor")" = 0x10de ] && case $(cat "$d/class") in 0x03*) GPU=found ;; esac
done
if [ $GPU = found ]; then
  GPU=$(ask --menu "Найдена видеокарта NVIDIA. Какой драйвер ставить?" 14 74 2 \
    nvidia  "проприетарный NVIDIA — быстрее, лучше для игр (несвободный)" \
    nouveau "свободный nouveau + NVK — для GTX 16xx/RTX и новее") || exit 1
fi

ask --yesno "Проверь:\n\nДиск:    $DISK  (будет стёрт!)\nХост:    $HOST\nЮзер:    $USERN\nПояс:    $TZ\nНаборы:  ${OPT:-нет}\nNVIDIA:  $GPU\n\nНачать установку?" 17 60 || exit 1
fi

# ---------- проверки (вторая линия защиты после графического установщика) ----------
echo "$USERN" | grep -Eq '^[a-z_][a-z0-9_-]{0,31}$' || die "Недопустимое имя пользователя: $USERN"
[ -n "$PW1" ] || die "Пустой пароль."
[ -e "/usr/share/zoneinfo/$TZ" ] || die "Нет такого часового пояса: $TZ"
case $MODE in
  disk|free) [ -b "$DISK" ] || die "Нет такого диска: $DISK" ;;
  part) [ -b "$PART" ] || die "Нет такого раздела: $PART"; DISK=$(disk_of "$PART") ;;
  *) die "Неизвестный способ установки: $MODE" ;;
esac
LIVE=$(live_disk)
[ -n "$LIVE" ] && [ "$DISK" = "$LIVE" ] && die "$DISK — это флешка с установщиком. Выбери другой диск."
case $MODE in
  disk) busy=$(lsblk -nrpo MOUNTPOINTS "$DISK" | grep -v '^$' || true) ;;
  free) busy= ;;
  part) busy=$(lsblk -nrpo MOUNTPOINTS "$PART" | grep -v '^$' || true) ;;
esac
[ -z "$busy" ] || die "Раздел на $DISK сейчас используется ($busy). Установка возможна только с флешки."
if [ "$MODE" = disk ] && [ -n "$(win_parts "$DISK")" ] && [ "$WIPE_OK" != "$DISK" ]; then
  die "На $DISK есть Windows — стирание диска не подтверждено."
fi
if [ "$MODE" = part ]; then
  info="$(blkid -o value -s TYPE "$PART" 2>/dev/null) $(lsblk -nro PARTTYPE "$PART" | head -1)"
  case $(echo "$info" | tr 'A-Z' 'a-z') in
    ntfs*|bitlocker*|vfat*|*c12a7328-f81f-11d2-ba4b-00a0c93ec93b*|*ebd0a0a2*|*e3c9e316*|*de94bba4*)
      die "Раздел $PART похож на Windows или EFI — его стирать нельзя." ;;
  esac
  [ -n "$ESP" ] || die "Для установки в раздел нужен существующий EFI-раздел."
fi
if [ -n "$ESP" ]; then
  [ -b "$ESP" ] || die "Нет EFI-раздела $ESP"
  [ "$(blkid -o value -s TYPE "$ESP")" = vfat ] || die "$ESP — не EFI-раздел (FAT32)."
fi

# ---------- разметка ----------
exec >>"$LOG" 2>&1   # дальше всё в лог; на экран — через fd 3
say() { N=$((N+1)); echo "@@STEP $N/$STEPS $*" >&3; echo "==> $*"; }
trap 'echo "@@ERROR Установка прервалась. Подробности: $LOG" >&3' EXIT
umount -R "$T" 2>/dev/null || true
parts() { lsblk -nrpo NAME,TYPE "$DISK" | awk '$2=="part"{print $1}' | sort; }
settle() { partx -u "$DISK" 2>/dev/null || partprobe "$DISK" 2>/dev/null || true; udevadm settle 2>/dev/null || sleep 2; }
# новый раздел после sfdisk --append: тот, которого не было в списке OLD
newpart() { parts | grep -vxF "$OLD" | head -1; }

say "Разметка диска $DISK"
echo "Было:"; sfdisk -d "$DISK" 2>/dev/null || true
case $MODE in
disk)
  wipefs -af "$DISK"
  if [ -n "$ESP" ] && [ "$(disk_of "$ESP")" = "$DISK" ]; then ESP=; fi   # EFI на стираемом диске пропадёт
  if [ -n "$ESP" ]; then printf 'label: gpt\n,,L\n' | sfdisk -q "$DISK"
  else printf 'label: gpt\n,512M,U\n,,L\n' | sfdisk -q "$DISK"; fi
  settle
  case $DISK in *[0-9]) PP=${DISK}p ;; *) PP=$DISK ;; esac
  if [ -n "$ESP" ]; then ROOT=${PP}1; else NEW_ESP=${PP}1; ROOT=${PP}2; fi
  ;;
free)
  set -- $(largest_free "$DISK")
  [ $# = 2 ] || die "На $DISK нет свободного места."
  if [ -z "$ESP" ]; then
    OLD=$(parts)
    printf 'start=%s, size=1048576, type=U\n' "$1" | sfdisk -q --append "$DISK"
    settle; NEW_ESP=$(newpart)
    set -- $(largest_free "$DISK")
  fi
  OLD=$(parts)
  printf 'start=%s, type=L\n' "$1" | sfdisk -q --append "$DISK"
  settle; ROOT=$(newpart)
  ;;
part)
  ROOT=$PART
  wipefs -af "$ROOT"
  ;;
esac
[ -b "${ROOT:-}" ] || die "Не удалось создать раздел для системы."
echo "Стало:"; sfdisk -d "$DISK"
if [ -n "${NEW_ESP:-}" ]; then
  [ -b "$NEW_ESP" ] || die "Не удалось создать EFI-раздел."
  mkfs.vfat -F32 -n EFI "$NEW_ESP"
  ESP=$NEW_ESP
fi
mkfs.ext4 -F -L lavender "$ROOT"
mkdir -p "$T"; mount "$ROOT" "$T"; mkdir -p "$T/boot/efi"; mount "$ESP" "$T/boot/efi"
# в EFI-разделе Windows обычно 100 МБ; GRUB нужно около 5 МБ
avail=$(df -Pk "$T/boot/efi" | awk 'NR==2{print $4}')
[ "$avail" -ge 8192 ] || die "В EFI-разделе $ESP меньше 8 МБ свободного места."

# ---------- пакеты ----------
say "Подготовка репозиториев"
mkdir -p "$T/var/db/xbps/keys" "$T/etc/xbps.d"
cp "$HERE"/keys/*.plist "$T/var/db/xbps/keys/"
cp "$HERE"/repos/*.conf "$T/etc/xbps.d/"

LIST=$(pkgs base.txt; pkgs desktop.txt; pkgs apps.txt; for s in $OPT; do section "$(echo "$s" | tr -d '"')"; done)
REPOS="-R $REPO -R $EXTRA_REPO"
case $GPU in
  nvidia)  REPOS="$REPOS -R $REPO/nonfree"; LIST="$LIST void-repo-nonfree nvidia" ;;
  nouveau) LIST="$LIST mesa-vulkan-nouveau linux-firmware-nvidia" ;;
esac
say "Установка пакетов ($(echo "$LIST" | wc -l) шт., это надолго)"
# shellcheck disable=SC2086
# LAVENDER_CACHE=папка с готовыми .xbps — не качать заново (тесты, офлайн-установка)
[ -n "${LAVENDER_CACHE:-}" ] && mkdir -p "$T/var/cache" && cp -a "$LAVENDER_CACHE" "$T/var/cache/xbps"
# зеркало Void иногда обновляется прямо во время установки → пакет «Not Found»; скачанное остаётся в кэше
for try in 1 2 3 4; do
  { XBPS_ARCH=x86_64 xbps-install -Sy -r "$T" $REPOS $LIST 2>&1; echo $? > /tmp/lavender-xbps.rc; } | tee -a "$LOG" >&3
  [ "$(cat /tmp/lavender-xbps.rc)" = 0 ] && break
  [ $try = 4 ] && exit 1
  echo "Ошибка загрузки, повтор $try/3 через 30 с" >&3; sleep 30
done

# ---------- настройка ----------
say "Настройка системы"
U2=$(blkid -s UUID -o value "$ROOT"); U1=$(blkid -s UUID -o value "$ESP")
cat > "$T/etc/fstab" <<E
UUID=$U2 / ext4 defaults 0 1
UUID=$U1 /boot/efi vfat defaults,umask=0077 0 2
tmpfs /tmp tmpfs defaults,nosuid,nodev 0 0
E
echo "$HOST" > "$T/etc/hostname"
ln -sf "/usr/share/zoneinfo/$TZ" "$T/etc/localtime"
sed -i 's/^#\(en_US.UTF-8\)/\1/; s/^#\(ru_RU.UTF-8\)/\1/' "$T/etc/default/libc-locales"
echo 'LANG=ru_RU.UTF-8' > "$T/etc/locale.conf"
printf 'KEYMAP=ru\nFONT=cyr-sun16\n' >> "$T/etc/rc.conf"
mkdir -p "$T/etc/sudoers.d"; echo '%wheel ALL=(ALL:ALL) ALL' > "$T/etc/sudoers.d/wheel"; chmod 440 "$T/etc/sudoers.d/wheel"

# overlay: оформление (/etc/skel), обои, экран входа, сессия, dconf — собирается build-overlay.sh
[ -d "$HERE/overlay" ] || die "Нет папки overlay/ — сначала запусти build-overlay.sh"
cp -a --no-preserve=ownership "$HERE/overlay/." "$T/"   # всё станет root:root

if [ -x "$T/usr/local/bin/lavender-greeter" ]; then
  GREETER="cage -s -d -- /usr/local/bin/lavender-greeter"
else
  GREETER="agreety --cmd /usr/local/bin/lavender-session"   # текстовый запасной вход
fi

# загрузочное меню: 5 с, запоминает последний выбор, лавандовые цвета; Windows — своим скриптом ниже
G=$T/etc/default/grub
setgrub() { sed -i "/^#\?$1=/d" "$G"; echo "$1=$2" >> "$G"; }
setgrub GRUB_DISTRIBUTOR '"Lavender OS"'
setgrub GRUB_TIMEOUT 5
setgrub GRUB_TIMEOUT_STYLE menu
setgrub GRUB_DEFAULT saved
setgrub GRUB_SAVEDEFAULT true
setgrub GRUB_DISABLE_OS_PROBER true   # os-prober в chroot ненадёжен — Windows ищет 35_windows
setgrub GRUB_COLOR_NORMAL '"light-gray/black"'
setgrub GRUB_COLOR_HIGHLIGHT '"black/light-magenta"'
install -Dm755 /dev/stdin "$T/etc/grub.d/35_windows" <<'E'
#!/bin/sh
# Lavender OS: пункт «Windows» для каждого EFI-раздела с загрузчиком Windows.
# Запускается grub-mkconfig (update-grub) — после переустановки Windows просто обнови GRUB.
set -e
tmp=$(mktemp -d)
for dev in $(blkid -o device -t TYPE=vfat 2>/dev/null); do
  m=$(findmnt -nro TARGET --source "$dev" 2>/dev/null | head -1); own=
  if [ -z "$m" ]; then mount -o ro "$dev" "$tmp" 2>/dev/null || continue; m=$tmp; own=1; fi
  if [ -f "$m/EFI/Microsoft/Boot/bootmgfw.efi" ]; then
    uuid=$(blkid -s UUID -o value "$dev")
    echo "Найдена Windows: $dev" >&2
    cat <<M
menuentry 'Windows' --class windows --class os {
	insmod part_gpt
	insmod fat
	search --no-floppy --fs-uuid --set=root $uuid
	chainloader /EFI/Microsoft/Boot/bootmgfw.efi
}
M
  fi
  [ -z "$own" ] || umount "$tmp"
done
rmdir "$tmp"
E

# NVIDIA: KMS для Wayland + совет niri против утечки видеопамяти
if [ "$GPU" = nvidia ]; then
  sed -i 's/^\(GRUB_CMDLINE_LINUX_DEFAULT="[^"]*\)"/\1 nvidia_drm.modeset=1 nvidia_drm.fbdev=1"/' "$G"
  echo 'blacklist nouveau' > "$T/etc/modprobe.d/nouveau-blacklist.conf"
  mkdir -p "$T/etc/nvidia/nvidia-application-profiles-rc.d"
  cat > "$T/etc/nvidia/nvidia-application-profiles-rc.d/50-niri.json" <<'E'
{ "rules": [ { "pattern": { "feature": "procname", "matches": "niri" }, "profile": "niri-heap" } ],
  "profiles": [ { "name": "niri-heap", "settings": [ { "key": "GLVidHeapReuseRatio", "value": 0 } ] } ] }
E
fi
cat > "$T/etc/greetd/config.toml" <<E
[terminal]
vt = 7
[default_session]
command = "env XKB_DEFAULT_LAYOUT=us,ru XKB_DEFAULT_OPTIONS=grp:ctrl_shift_toggle $GREETER"
user = "_greeter"
E

for d in dev proc sys run; do mount --rbind "/$d" "$T/$d"; mount --make-rslave "$T/$d"; done
cp /etc/resolv.conf "$T/etc/"
chroot "$T" /bin/sh -eu <<E
xbps-reconfigure -f glibc-locales
useradd -m -s /bin/zsh -G wheel,audio,video,input,network,bluetooth "$USERN"
grep -q '^_greeter:' /etc/passwd || useradd -r -M -s /sbin/nologin -G video,input _greeter
for s in dbus elogind NetworkManager chronyd bluetoothd tlp greetd; do
  [ -d /etc/sv/\$s ] && ln -sf /etc/sv/\$s /etc/runit/runsvdir/default/
done
rm -f /etc/runit/runsvdir/default/agetty-tty7
# оформление: пути @HOME@ из /etc/skel → домашняя папка, папки Papirus фиолетовые, GTK-умолчания
H=/home/$USERN
grep -rlI '@HOME@' \$H | xargs -r sed -i "s|@HOME@|\$H|g"
su -s /bin/sh "$USERN" -c 'LC_ALL=C xdg-user-dirs-update' || true   # ~/Pictures, ~/Downloads ... (англ. имена, как в конфигах)
papirus-folders -C violet --theme Papirus-Dark || true
dconf update
install -d -o _greeter -g _greeter /var/cache/lavender-greeter
echo "$USERN" > /var/cache/lavender-greeter/last; chown _greeter: /var/cache/lavender-greeter/last
xbps-reconfigure -fa
E
# -c SHA512: без PAM (через PAM в chroot пароль молча не записывался)
printf '%s:%s\n' "$USERN" "$PW1" | chroot "$T" chpasswd -c SHA512
grep -q "^$USERN:\\\$6\\\$" "$T/etc/shadow" || { echo "@@ERROR Пароль не записан" >&3; exit 1; }
chroot "$T" passwd -l root >/dev/null   # root без пароля-входа; админ через sudo

# ---------- загрузчик ----------
say "Загрузчик и поиск Windows"
chroot "$T" grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=Lavender
chroot "$T" grub-mkconfig -o /boot/grub/grub.cfg
if grep -q "^menuentry 'Windows'" "$T/boot/grub/grub.cfg"; then
  echo "@@INFO В меню загрузки добавлена Windows" >&3
  # Windows хранит в часах местное время — иначе время в Windows будет сбиваться
  sed -i '/^#\?HARDWARECLOCK=/d' "$T/etc/rc.conf"; echo 'HARDWARECLOCK="localtime"' >> "$T/etc/rc.conf"
fi
efibootmgr 2>/dev/null | grep -i lavender || true

say "Завершение"
sync
umount -R "$T"
trap - EXIT
echo "@@DONE" >&3
echo "Готово! Вынь флешку и перезагрузись: reboot" >&3
