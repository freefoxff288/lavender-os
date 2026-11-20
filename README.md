# Lavender OS

Дистрибутив на основе Void Linux: niri + noctalia, лавандовое оформление, только свободный софт.

## Структура
- `packages/base.txt` — основа системы (загрузка, сеть, звук, графика)
- `packages/desktop.txt` — рабочий стол, шрифты, темы
- `packages/apps.txt` — приложения по умолчанию
- `packages/optional.txt` — наборы на выбор при установке: `[media]`, `[dev]`, `[fun]`
- `keys/`, `repos/` — ключи и конфиг стороннего репозитория (noctalia, adw-gtk-theme)
- `build-overlay.sh` — собирает `overlay/` из твоего текущего рабочего стола (запускать после изменений оформления)
- `overlay/` — файлы для корня новой системы: конфиги в `/etc/skel`, обои, lavender-greeter, сессия, dconf
- `installer/` — графический установщик (GTK4): выбор диска, дуалбут с Windows, прогресс
- `install.sh` — то, что реально ставит систему (его запускает графический установщик; без него — вопросы в whiptail)
- `apps/` — исходники своих программ (см. ниже)
- `build-iso.sh` — собирает установочный ISO (void-mklive, нужен root)
- `test-vm.sh`, `tools/` — проверка в виртуальной машине QEMU

## Свои программы
Исходники в `apps/`, ставятся в `/usr/local/bin`, собирает их `build-overlay.sh`:
- `dropnotes` — выпадающие заметки (Mod+N)
- `lavwall` — минималистичные обои в цветах палитры; кнопка «Новые обои» в Control Center (Super+A)
- `lavshot` — красивые скриншоты: Super+Print — окно, Super+Shift+Print — область
- `lavtop` — диспетчер задач в терминале
- `lavender-greeter` — экран входа
- `devjoke` — шутка про программистов в приветствии noctalia

## Подготовка
`sh build-overlay.sh` — без root, собирает `overlay/` из текущего рабочего стола (нужен только автору,
готовый `overlay/` уже лежит в репозитории). В конце скрипт предупредит, если в конфиги попали личные пути.

Сборка с нуля на другом компьютере с Void Linux:
```sh
sudo xbps-install -S git gtk4-devel json-glib-devel gtk4-layer-shell-devel make gcc xorriso squashfs-tools
git clone https://github.com/freefoxff288/lavender-os && cd lavender-os
sudo sh build-iso.sh
```

## Установка (инструкция)
Требования: компьютер с UEFI, интернет (кабель или Wi-Fi), флешка от 4 ГБ.

1. Скачать готовый ISO из [Releases](https://github.com/freefoxff288/lavender-os/releases/latest)
   (проверка: `sha256sum -c lavender-os-*.iso.sha256`) — или собрать самому: `sudo sh build-iso.sh` → `build/lavender-os-ДАТА.iso`.
2. Записать на флешку (sdX — флешка, проверь через `lsblk`! В Windows — Rufus в режиме DD или balenaEtcher):
   `sudo dd if=build/lavender-os-*.iso of=/dev/sdX bs=4M status=progress oflag=sync`
3. Загрузиться с флешки в режиме **UEFI** (меню загрузки — обычно F8/F11/F12/Esc).
4. Сам откроется графический установщик: проверка UEFI и интернета (Wi-Fi можно подключить прямо в нём) →
   выбор диска и способа → имя, пароль, часовой пояс → наборы → экран «Что произойдёт» → «Установить».
5. После «Готово» — «Перезагрузить» и вынуть флешку.

Если установщик закрыт — запустить снова: `lavender-installer`. Текстовый запасной (стирает весь диск): `lavender-install`.
Лог установки: `/tmp/lavender-install.log`.

## Рядом с Windows (дуалбут)
Перед установкой, в Windows:
1. **Освободить место**: Win+X → «Управление дисками» → правый клик по диску C: → «Сжать том» → от 20 ГБ (лучше 60+).
   Появится «Не распределено» — туда установщик и поставит систему (способ «В свободное место»).
   Ничего не создавай и не форматируй в этом месте — установщик сделает сам.
2. **Отключить быстрый запуск**: Панель управления → Электропитание → «Действия кнопок питания» →
   снять «Включить быстрый запуск». Иначе Windows оставляет диски «наполовину выключенными».
3. **BitLocker**: если включён — сохрани ключ восстановления (account.microsoft.com/devices/recoverykey).
   После установки Windows может один раз его попросить.
4. **Secure Boot** в настройках UEFI выключить (GRUB Lavender OS не подписан).

Установщик сам находит Windows (по разделам NTFS/BitLocker/служебным разделам Microsoft и загрузчику
`EFI/Microsoft/Boot/bootmgfw.efi`) и помечает такие диски. Флешку с установщиком выбрать нельзя.
Способы:
- **В свободное место** — существующие разделы не меняются, загрузчик кладётся в EFI-раздел Windows без форматирования.
- **Стереть весь диск** — для отдельного диска. Если на нём есть данные, нужно вписать имя диска (например `sda`).
- **Заменить раздел** — форматирует один не-Windows раздел от 20 ГБ (например старый Linux).

### Загрузочное меню
GRUB: 5 секунд, пункты **Lavender OS** и **Windows**, запоминает последний выбор.
Пункт Windows добавляет скрипт `/etc/grub.d/35_windows` — после переустановки Windows: `sudo update-grub`.
Часы при найденной Windows идут по местному времени (`HARDWARECLOCK="localtime"`), чтобы время не сбивалось.

Если после установки компьютер сразу грузит Windows — в UEFI (F2/Del) в Boot Order поставить **Lavender** первым,
или в Linux: `sudo efibootmgr` → `sudo efibootmgr -o XXXX,YYYY` (номер Lavender первым).
Если большое обновление Windows вернуло себя первой — то же самое.

## Что делает установщик
Разметка (весь диск: GPT 512M EFI + ext4; свободное место: новый раздел ext4, EFI переиспользуется) → пакеты через `xbps-install -r` → fstab, локаль ru/en, часовой пояс →
пользователь в группе wheel (sudo), root заблокирован → службы runit (dbus, elogind,
NetworkManager, chronyd, bluetoothd, tlp, greetd) → GRUB (EFI) → вход через lavender-greeter в niri.

NVIDIA определяется автоматически, на выбор: проприетарный `nvidia` (из nonfree, с
`nvidia_drm.modeset=1` и фиксом видеопамяти для niri) или свободный nouveau + NVK.
