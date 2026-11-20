#!/bin/sh
# Тестовая VM (UEFI, KVM). Без root.
#   sh test-vm.sh install [iso]  — загрузка с ISO, диск build/vm/disk.qcow2 (создаётся заново)
#   sh test-vm.sh boot           — загрузка установленной системы с диска
# HEADLESS=1 — без окна; управление: python3 tools/vmctl.py (сокет build/vm/monitor.sock)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
V=$HERE/build/vm; mkdir -p "$V"
MODE=${1:-boot}; ISO=${2:-}
OVMF=/usr/share/edk2/x64
[ -f "$V/vars.fd" ] && [ "$MODE" != install ] || cp "$OVMF/OVMF_VARS.4m.fd" "$V/vars.fd"
set -- -enable-kvm -cpu host -smp 4 -m 4096 -machine q35 \
  -drive if=pflash,format=raw,readonly=on,file=$OVMF/OVMF_CODE.4m.fd \
  -drive if=pflash,format=raw,file=$V/vars.fd \
  -device virtio-vga-gl -device virtio-net,netdev=n -netdev user,id=n \
  -device qemu-xhci -device usb-tablet \
  -monitor unix:$V/monitor.sock,server,nowait
if [ "$MODE" = install ]; then
  [ -n "$ISO" ] || ISO=$(ls -t "$HERE"/build/lavender-os-*.iso | head -1)
  rm -f "$V/disk.qcow2"; qemu-img create -q -f qcow2 "$V/disk.qcow2" 30G
  set -- "$@" -cdrom "$ISO" -boot d
fi
set -- "$@" -drive file=$V/disk.qcow2,if=virtio,format=qcow2
if [ -n "${HEADLESS:-}" ]; then set -- "$@" -display egl-headless; else set -- "$@" -display gtk,gl=on; fi
exec qemu-system-x86_64 "$@"
