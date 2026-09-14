#!/usr/bin/env bash
# =============================================================================
# GITHUBCLOAD GUI (Linux): запуск из исходников.
# Создаёт/использует venv (GCB_VENV, по умолчанию .venv-linux), ставит
# py7zr и PyGithub и запускает python3 GITHUBCLOAD_GUI.py.
#
# Использование:   bash run_gui_linux.sh [--smoke | --version]
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

info() { printf '[INFO] %s\n' "$*"; }
err()  { printf '[ОШИБКА] %s\n' "$*" >&2; }

# --- python3 -------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  err "не найден python3. Установите:"
  err "  Debian/Ubuntu:  sudo apt install -y python3 python3-venv python3-tk tk"
  exit 1
fi

# --- tkinter (GUI без него не работает) ----------------------------------------
if ! python3 -c "import tkinter" >/dev/null 2>&1; then
  err "модуль tkinter недоступен — графический интерфейс не запустится."
  err "Установите пакет python3-tk (в Debian/Ubuntu он НЕ ставится вместе с python3):"
  err "  Debian/Ubuntu:  sudo apt install -y python3-tk tk"
  err "  Fedora:         sudo dnf install -y python3-tkinter tk"
  err "  Arch:           sudo pacman -S tk"
  exit 1
fi

# --- venv -----------------------------------------------------------------------
VENV_DIR="${GCB_VENV:-.venv-linux}"
if [ ! -x "$VENV_DIR/bin/python" ]; then
  info "создаю виртуальное окружение: $VENV_DIR"
  python3 -m venv "$VENV_DIR" || {
    err "не удалось создать venv. На Debian/Ubuntu нужен пакет python3-venv:"
    err "  sudo apt install -y python3-venv"
    exit 1
  }
fi
PY="$VENV_DIR/bin/python"

info "проверяю зависимости (py7zr, PyGithub)..."
"$PY" -m pip install --disable-pip-version-check --quiet --upgrade pip
"$PY" -m pip install --disable-pip-version-check --quiet -r requirements.txt

# --- секреты (только проверка наличия, содержимое не читается) -------------------
[ -f tokengh.txt ] || info "tokengh.txt не найден рядом с GUI — команды GitHub не заработают"
[ -f PBEpass.txt ] || info "PBEpass.txt не найден рядом с GUI — шифрование не заработает"

info "запускаю: $PY GITHUBCLOAD_GUI.py $*"
exec "$PY" GITHUBCLOAD_GUI.py "$@"
