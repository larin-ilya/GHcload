#!/usr/bin/env bash
# =============================================================================
# GITHUBCLOAD GUI (Linux): воспроизводимая сборка в Docker-контейнере.
#
# Базовый образ: python:3.11-slim-bookworm. Внутри контейнера ставятся
# tk (+ binutils для PyInstaller), создаётся venv и запускается
# build_gui_linux.sh. Папка проекта монтируется в /work, результат
# появляется в dist/GITHUBCLOAD_GUI на хосте.
#
# Использование:   bash build_linux_docker.sh
# Переменные:      GCB_IMAGE   образ (по умолчанию python:3.11-slim-bookworm)
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

info() { printf '[INFO] %s\n' "$*"; }
err()  { printf '[ОШИБКА] %s\n' "$*" >&2; }

IMAGE="${GCB_IMAGE:-python:3.11-slim-bookworm}"

# --- docker -------------------------------------------------------------------
if ! command -v docker >/dev/null 2>&1; then
  err "не найден docker. Установите Docker Desktop / docker engine и повторите."
  exit 1
fi

# --- путь к папке проекта ------------------------------------------------------
# В Git Bash (Windows) нужен Windows-путь для монтирования: pwd -W.
if HOST_DIR="$(pwd -W 2>/dev/null)"; then
  [ -n "$HOST_DIR" ] || HOST_DIR="$(pwd)"
else
  HOST_DIR="$(pwd)"
fi
info "папка проекта: $HOST_DIR"
info "образ: $IMAGE"

mkdir -p dist

# MSYS2/Git Bash: отключаем преобразование путей в аргументах docker.
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL="*"

# На Linux-хосте файлы из контейнера будут принадлежать root —
# передаём UID/GID, чтобы вернуть владение пользователю.
HOST_UID="$(id -u 2>/dev/null || echo 0)"
HOST_GID="$(id -g 2>/dev/null || echo 0)"

info "запускаю сборку в контейнере..."
docker run --rm -i \
  -e HOST_UID="$HOST_UID" -e HOST_GID="$HOST_GID" \
  -v "$HOST_DIR:/work" -w /work \
  "$IMAGE" bash -s <<'INNER'
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

echo "[docker] python: $(python3 -VV)"
echo "[docker] ставлю tk, python3-tk, binutils, xvfb..."
apt-get update -qq
apt-get install -y -qq tk python3-tk binutils xvfb file >/dev/null

echo "[docker] сборка через build_gui_linux.sh (venv в /tmp, чтобы не мусорить в проекте)..."
GCB_VENV=/tmp/gcb-venv bash build_gui_linux.sh

echo "[docker] контроль: артефакт и headless-запуск..."
ls -l dist/GITHUBCLOAD_GUI
if command -v xvfb-run >/dev/null 2>&1; then
  xvfb-run -a dist/GITHUBCLOAD_GUI --version
  xvfb-run -a dist/GITHUBCLOAD_GUI --smoke
else
  echo "[docker] xvfb-run недоступен — smoke-проверка артефакта пропущена"
fi

# вернуть владение файлы хостовому пользователю (актуально для Linux-хостов)
if [ "${HOST_UID:-0}" != "0" ] && [ "${HOST_GID:-0}" != "0" ]; then
  chown -R "${HOST_UID}:${HOST_GID}" dist build_gui 2>/dev/null || true
fi
INNER

info "Готово. Артефакт: dist/GITHUBCLOAD_GUI"
ls -l dist/GITHUBCLOAD_GUI
command -v file >/dev/null 2>&1 && file dist/GITHUBCLOAD_GUI || true
