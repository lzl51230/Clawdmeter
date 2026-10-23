#!/bin/bash

# Finder 启动时工作目录和 PATH 不一定与交互式终端相同。
fail() {
    printf '%s\n' "$@" >&2
    if [ -t 0 ]; then
        read -r -p '按回车键退出…' reply
    fi
    exit 1
}

cd "$(dirname "$0")" || fail '无法进入项目目录。'

python=''
for candidate in "$(command -v python3 2>/dev/null)" /opt/homebrew/bin/python3 /usr/local/bin/python3; do
    if [ -x "$candidate" ] && "$candidate" -c 'import sys; sys.exit(sys.version_info < (3, 11))' 2>/dev/null; then
        python="$candidate"
        break
    fi
done
[ -n "$python" ] || fail '需要 Python 3.11 或更新版本。请安装后重新双击启动。'

needs_serial=true
for arg in "$@"; do
    case "$arg" in
        --dry-run|--help|-h) needs_serial=false ;;
    esac
done
if "$needs_serial" && ! "$python" -c 'import serial; from serial.tools import list_ports' 2>/dev/null; then
    quoted_python=${python//\'/\'\\\'\'}
    install_command="'$quoted_python' -m pip install --user pyserial"
    fail '缺少 pyserial，请先在终端运行：' "$install_command" \
        '如果 Homebrew 提示 externally-managed-environment，请在安装命令中加上 --break-system-packages。'
fi

printf 'Codex 串口同步已启动，按 Ctrl+C 停止。\nPython: %s\n' "$python"
"$python" -u tools/windows_codex_usage_serial.py \
    --watch --follow-device --port auto --baud 115200 --poll-interval 60 --retry-delay 5 "$@"
result=$?
if [ "$result" -ne 0 ] && [ -t 0 ]; then
    printf '启动失败（退出码 %s），请查看上方错误。\n' "$result" >&2
    read -r -p '按回车键退出…' reply
fi
exit "$result"
