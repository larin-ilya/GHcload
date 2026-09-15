#!/usr/bin/env bash
# =============================================================================
# GITHUBCLOAD GUI (Linux): сборка ЕДИНОГО самодостаточного исполняемого файла
# dist/GITHUBCLOAD_GUI через PyInstaller (--onefile, GUI-режим).
#
# Внутрь сборки включается движок GITHUBCLOAD.py (hidden-import), а также
# py7zr, multivolumefile и PyGithub — готовый файл запускается без Python.
# tokengh.txt / PBEpass.txt в сборку НЕ попадают: их кладут рядом с
# dist/GITHUBCLOAD_GUI (движок ищет их рядом с исполняемым файлом).
# Иконки app.png (окно) и app.ico кладутся внутрь сборки: GUI читает PNG из
# распакованного _MEIPASS. Для меню Linux рядом лежит GITHUBCLOAD_GUI.desktop.
#
# Скрипт сам создаёт/использует venv (GCB_VENV, по умолчанию .venv-linux)
# и ставит туда py7zr, PyGithub и pyinstaller.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

info() { printf '[INFO] %s\n' "$*"; }
warn() { printf '[ВНИМАНИЕ] %s\n' "$*" >&2; }
err()  { printf '[ОШИБКА] %s\n' "$*" >&2; }

# --- python3 -----------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  err "не найден python3. Установите его:"
  err "  Debian/Ubuntu:  sudo apt install -y python3 python3-venv python3-tk tk"
  exit 1
fi
info "python3: $(python3 -VV)"

# --- venv --------------------------------------------------------------------
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

# --- tkinter обязателен и для сборки (PyInstaller пакует его) ----------------
if ! "$PY" -c "import tkinter" >/dev/null 2>&1; then
  err "в сборочном интерпретаторе ($PY) недоступен tkinter."
  err "GUI собирать нельзя — в бинарник не попадёт ни интерфейс, ни Tk."
  err "Установите графическую библиотеку и пересоберите:"
  err "  Debian/Ubuntu:  sudo apt install -y python3-tk tk"
  err "  Fedora:         sudo dnf install -y python3-tkinter tk"
  err "  Arch:           sudo pacman -S tk"
  exit 1
fi

# --- зависимости --------------------------------------------------------------
info "ставлю зависимости (py7zr, PyGithub, pyinstaller)..."
"$PY" -m pip install --disable-pip-version-check --quiet --upgrade pip
"$PY" -m pip install --disable-pip-version-check --quiet -r requirements.txt pyinstaller

# --- сборка -------------------------------------------------------------------
if [ ! -f GITHUBCLOAD.py ]; then
  err "рядом со скриптом нет GITHUBCLOAD.py — движок нечем включать в сборку."
  exit 1
fi
if [ ! -f GITHUBCLOAD_GUI.py ]; then
  err "рядом со скриптом нет GITHUBCLOAD_GUI.py."
  exit 1
fi

# --- иконка --------------------------------------------------------------------
# app.ico задаёт иконку самой сборки (на Linux PyInstaller его игнорирует, но
# команда остаётся корректной и для других платформ), app.png — иконка окна:
# GUI читает её из распакованной сборки (_MEIPASS), поэтому файл кладём внутрь.
# Пути к файлам-данным — абсолютные: относительные PyInstaller ищет рядом со
# .spec-файлом (--specpath build_gui), а не в корне проекта.
if [ ! -f app.ico ]; then
  err "рядом со скриптом нет app.ico — нечем задать иконку сборки (--icon app.ico)."
  err "Восстановите app.ico из репозитория проекта и повторите сборку."
  exit 1
fi
PROJECT_DIR="$(pwd)"
ICON_ARGS=(--icon "$PROJECT_DIR/app.ico"
           --add-data "$PROJECT_DIR/app.ico:.")
if [ -f app.png ]; then
  ICON_ARGS+=(--add-data "$PROJECT_DIR/app.png:.")
else
  warn "нет app.png — иконка окна НЕ попадёт внутрь сборки, окно получит значок по умолчанию."
  warn "Положите app.png (256x256, конвертация из app.ico) рядом со скриптом и пересоберите."
fi

info "PyInstaller: собираю dist/GITHUBCLOAD_GUI (--onefile, GUI)..."
rm -f dist/GITHUBCLOAD_GUI
"$PY" -m PyInstaller \
  --noconfirm --clean \
  --onefile \
  --windowed \
  --name GITHUBCLOAD_GUI \
  --hidden-import GITHUBCLOAD \
  "${ICON_ARGS[@]}" \
  --distpath dist \
  --workpath build_gui \
  --specpath build_gui \
  GITHUBCLOAD_GUI.py

# --- результат ----------------------------------------------------------------
if [ ! -f dist/GITHUBCLOAD_GUI ]; then
  err "сборка не создала dist/GITHUBCLOAD_GUI."
  exit 1
fi
chmod +x dist/GITHUBCLOAD_GUI

info "Готово: dist/GITHUBCLOAD_GUI ($(du -h dist/GITHUBCLOAD_GUI | cut -f1))"
command -v file >/dev/null 2>&1 && file dist/GITHUBCLOAD_GUI || true
info "Положите рядом с бинарником tokengh.txt и PBEpass.txt — и запускайте:"
info "  ./dist/GITHUBCLOAD_GUI"
info "Headless-проверка: xvfb-run -a dist/GITHUBCLOAD_GUI --smoke"
info "Меню Linux: GITHUBCLOAD_GUI.desktop + app.png (см. README, раздел про Linux)."
