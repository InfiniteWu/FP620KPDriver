#!/usr/bin/env bash
#
# Jolimark FP-620K+ CUPS driver installer.
#
# Usage:
#   ./install.sh              install filter, backend and PPD, create queue FP620K
#   ./install.sh --test       install and print the calibration page
#   ./install.sh --uninstall  remove the queue and the installed driver files
#
# The script re-runs itself through sudo because the filter, backend and PPD
# live in system directories.  Set PRINTER_NAME to use a different queue name
# and FP620K_DEVICE to use a device node other than /dev/usb/lp0.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PRINTER_NAME="${PRINTER_NAME:-FP620K}"
DEVICE="${FP620K_DEVICE:-/dev/usb/lp0}"
PPD_NAME="jolimark-fp620kp.ppd"
MODE="install"

case "${1:-}" in
    --test)      MODE="install-test" ;;
    --uninstall) MODE="uninstall" ;;
    "")          MODE="install" ;;
    *)
        echo "用法: $0 [--test|--uninstall]" >&2
        exit 1
        ;;
esac

if [ "$MODE" = "uninstall" ]; then
    if [ "$(id -u)" -ne 0 ]; then
        exec sudo -- "$0" "$@"
    fi
    lpadmin -x "$PRINTER_NAME" 2>/dev/null || true
    make -C "$SCRIPT_DIR" uninstall
    echo "已删除队列 $PRINTER_NAME 和驱动文件。"
    exit 0
fi

if ! command -v cups-config >/dev/null 2>&1; then
    echo "错误: 找不到 cups-config，请先安装 libcups2-dev。" >&2
    exit 1
fi
if ! command -v lpadmin >/dev/null 2>&1; then
    echo "错误: 找不到 lpadmin，请先安装 cups。" >&2
    exit 1
fi
if [ ! -e "$DEVICE" ]; then
    echo "错误: 设备节点 $DEVICE 不存在。" >&2
    echo "请确认打印机已开机、已连接，并已加载 usblp 驱动（ls /dev/usb/lp*）。" >&2
    exit 1
fi

echo "正在编译 ..."
make -C "$SCRIPT_DIR"

if [ "$(id -u)" -ne 0 ]; then
    echo "安装驱动文件需要 root 权限，接下来会调用 sudo ..."
    exec sudo -- "$0" "$@"
fi

make -C "$SCRIPT_DIR" install

lpadmin -p "$PRINTER_NAME" -E \
    -v "fp620kp:$DEVICE" \
    -P "/usr/share/cups/model/$PPD_NAME" \
    -o Resolution=360x180dpi -o PrintDirection=Unidirectional -o PageSize=A4
lpadmin -d "$PRINTER_NAME"

echo
echo "已安装队列: $PRINTER_NAME"
echo "  设备: fp620kp:$DEVICE"
echo "  查看: lpstat -p $PRINTER_NAME -l"
echo "  打印: lp -d $PRINTER_NAME 文件.pdf"

if [ "$MODE" = "install-test" ]; then
    echo "正在打印校准页 ..."
    lp -d "$PRINTER_NAME" -o Resolution=360x180dpi \
        -o PrintDirection=Unidirectional -o PageSize=A4 \
        -t "FP620K calibration" "$SCRIPT_DIR/tests/calibration.ps"
fi
