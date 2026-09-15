#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GITHUBCLOAD_GUI.py — графический интерфейс для движка GITHUBCLOAD.py.

SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 larin-ilya
Лицензия — LICENSE, правовые предупреждения — LEGAL.md.

Кросс-платформенный (Linux-first), только стандартная библиотека (tkinter).
Набор возможностей повторяет Windows-версию (GCB_GUI.cpp):
  * «Хранилища»    — список хранилищ; двойной клик открывает содержимое
                     (элементы с отметками, кнопки «Скачать» на строке,
                     «Скачать выбранные», «Удалить» по id, «Стереть» = wipe);
  * «Загрузка»     — файл/папка + имя хранилища -> «Загрузить в облако»;
  * «Самопроверка» — прогон selftest движка без GitHub;
  * журнал внизу   — вывод команд движка построчно в реальном времени.

Движок GITHUBCLOAD.py НЕ изменяется: GUI импортирует его и вызывает
main([...]) в рабочем потоке, перехватывая sys.stdout/sys.stderr (и SystemExit).
Если импорт невозможен — корректный откат на запуск `python3 GITHUBCLOAD.py ...`
сабпроцессом (в собранном onefile-виде файла рядом нет, ошибка будет понятной).

Служебные флаги:
  --version       печатает версию GUI и завершает работу с кодом 0;
  --smoke         строит окно и все три страницы, обновляет layout, закрывает окно
                  и завершает работу с кодом 0 (headless-проверка под Xvfb);
  --accept-terms  принимает условия использования без диалога (пишет _gcb_terms.json
                  рядом с программой) и продолжает обычный запуск;
  --help          краткая справка.

При первом запуске (пока рядом нет _gcb_terms.json нужной версии условий) показывается
модальное окно подтверждения условий использования. Отказ или закрытие окна завершают
программу с кодом 3, ничего не изменяя. Флаги --smoke, --version, --help и --accept-terms
окно не показывают — это нужно для headless-проверок.

Иконка окна берётся из app.png (256x256): сначала из распакованной сборки
(sys._MEIPASS), затем из папки приложения. Нет файла — работаем со значком по
умолчанию, запуск не ломается. При каждом запуске (кроме --version и --help) в stderr
и в журнал выводится одна короткая строка правового предупреждения.

Примеры:
  python3 GITHUBCLOAD_GUI.py
  xvfb-run -a python3 GITHUBCLOAD_GUI.py --smoke
"""

import datetime
import importlib.util
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import traceback
import webbrowser

GUI_NAME = "GITHUBCLOAD_GUI"
GUI_VERSION = "1.1.0"
ENGINE_MODULE = "GITHUBCLOAD"
ENGINE_SCRIPT = "GITHUBCLOAD.py"

# --- подтверждение условий использования и правовые документы ---------------
TERMS_VERSION = "1.0"                     # версия условий; менять вместе с LEGAL.md
TERMS_FILE = "_gcb_terms.json"            # файл состояния (создаётся в папке приложения)
LEGAL_FILE = "LEGAL.md"                   # правовые предупреждения
LICENSE_FILE = "LICENSE"                  # текст лицензии AGPL-3.0-or-later
ICON_FILE = "app.png"                     # иконка окна (PNG 256x256; в сборке — внутри _MEIPASS)
# Адрес проекта — запасной источник документов, когда файлов рядом с программой
# нет (например, у собранного onefile-бинарника).
PROJECT_URL = "https://github.com/larin-ilya/GITHUBCLOAD"

# Одна короткая строка правового предупреждения: печатается при запуске в консоль
# (stderr) и в журнал GUI — ровно один раз, а не при каждой операции.
# Формулировка согласована с разделом «Правовая информация» в --help и с LEGAL.md.
NOTICE_TEXT = (
    "GITHUBCLOAD %s · GNU AGPL-3.0-or-later · поставляется «как есть»; "
    "полные правовые предупреждения — %s, лицензия — %s"
    % (GUI_VERSION, LEGAL_FILE, LICENSE_FILE)
)

# --------------------------------------------------------------------------
# tkinter может отсутствовать (например, python3 без пакета python3-tk).
# Импортируем аккуратно: --version и понятное сообщение об ошибке должны
# работать даже без графической библиотеки.
# --------------------------------------------------------------------------
try:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox, font as tkfont
    TK_ERROR = None
except Exception as _tk_exc:                      # pragma: no cover
    tk = None
    ttk = None
    filedialog = None
    messagebox = None
    tkfont = None
    TK_ERROR = _tk_exc

# --------------------------------------------------------------------------
# Оформление
# --------------------------------------------------------------------------
C_BG = "#eef1f6"
C_PANEL = "#ffffff"
C_TEXT = "#1f2430"
C_MUTED = "#6b7280"
C_BORDER = "#d5dbe5"
C_ACCENT = "#2f6fed"
C_ACCENT_DARK = "#1f56c4"
C_ACCENT_SOFT = "#e8eefb"
C_DANGER = "#d64545"
C_LOG_BG = "#11151c"
C_LOG_FG = "#d7dee8"
C_OK = "#1a9e4b"
C_ERR = "#c0392b"

MAX_LOG_LINES = 5000                             # защита журнала от разрастания

UPLOAD_INFO = (
    "Что произойдёт: файл/папка шифруются паролем из PBEpass.txt (7z, AES-256), "
    "при размере больше 8 МБ архив автоматически режется на части по 8 МБ, "
    "после чего части заливаются в приватный репозиторий-том GitHub по токену "
    "из tokengh.txt.\n"
    "Служебные файлы tokengh.txt и PBEpass.txt никогда не попадают в репозиторий, "
    "даже если лежат внутри загружаемой папки."
)

SELFTEST_INFO = (
    "Самопроверка не обращается к GitHub: движок создаёт тестовые файлы, шифрует их "
    "паролем, режет архив на части, склеивает обратно, расшифровывает, а затем "
    "сверяет содержимое и проверяет, что tokengh.txt / PBEpass.txt в архив не попали.\n"
    "При успехе в журнале появится строка «САМОПРОВЕРКА ПРОЙДЕНА УСПЕШНО» и код возврата 0."
)

USAGE = """GITHUBCLOAD_GUI %s — графический интерфейс для GITHUBCLOAD.py

Запуск:            python3 GITHUBCLOAD_GUI.py
Служебные флаги:   --version       версия GUI (код 0)
                   --smoke         headless-проверка окна и страниц (код 0)
                   --accept-terms  принять условия использования без диалога
                                   (создаёт %s рядом с программой) и продолжить запуск
                   --help          эта справка

При первом запуске один раз на папку приложения показывается окно подтверждения
условий использования; согласие сохраняется в %s. Удалите этот файл, чтобы увидеть
окно снова. Флаги --version, --smoke и --accept-terms окно не показывают.

Правовая информация: лицензия GNU AGPL-3.0-or-later, программа поставляется
«как есть», без гарантий. Та же строка печатается при запуске — в консоль (stderr)
и в журнал GUI:
  %s

Раздел «Правовая информация» есть и на странице «Самопроверка». Полные тексты —
%s (предупреждения и допустимое использование) и %s (лицензия) рядом с программой,
в репозитории проекта %s и в релизе.

Рядом с GUI (или рядом с собранным файлом dist/GITHUBCLOAD_GUI) должны лежать
tokengh.txt (токен GitHub) и PBEpass.txt (пароль шифрования).
""" % (GUI_VERSION, TERMS_FILE, TERMS_FILE, NOTICE_TEXT, LEGAL_FILE, LICENSE_FILE,
       PROJECT_URL)

# Краткий юридический текст для окна подтверждения условий (clickwrap).
# Полные тексты — LEGAL.md и LICENSE; здесь только то, что важно знать до запуска.
TERMS_TEXT = """GITHUBCLOAD хранит зашифрованные файлы в ВАШИХ собственных репозиториях GitHub.
Программа бесплатна и поставляется «как есть». До запуска прочитайте:
1. Никаких гарантий («as is»): явных или подразумеваемых гарантий нет, включая
   пригодность для конкретной цели и сохранность данных. Используете — на свой риск.
2. Риск утраты данных — на вас: автор не отвечает за потерю или повреждение файлов,
   утечку токена, а также за ограничения и удаление ваших репозиториев на GitHub.
3. Пароль шифрования не восстанавливается: мастер-ключа и «кода восстановления» нет.
   Потеря PBEpass.txt = необратимая потеря данных; резервные копии — ваша задача.
4. Метаданные не шифруются: имя файла/папки, размер и список частей архива лежат
   открытым текстом в _gcb_manifest.json внутри репозитория. Учитывайте это.
5. Правила GitHub соблюдать обязательно: репозитории — не хранилище и не бэкап
   общего назначения, обходить лимиты нельзя — аккаунт и репозитории ограничат.
6. Запрещены нелегальные материалы (в том числе с участием несовершеннолетних),
   вредоносный код, спам и фишинг, чужие данные и персональные данные третьих лиц
   без законного основания. Шифрование не делает такую обработку законной.
7. Ответственность — на пользователе: только вы отвечаете за то, что и куда
   загружаете, за сохранность токена и пароля и за соблюдение законов своей
   юрисдикции (включая экспортный контроль).
8. Проект не связан с GitHub, Inc., не спонсируется и не поддерживается им;
   «GitHub» — торговая марка GitHub, Inc.
9. Лицензия — GNU AGPL-3.0-or-later (файл LICENSE). Полные правовые предупреждения
   и правила допустимого использования — в файле LEGAL.md.

Нажимая «Принимаю условия», вы подтверждаете, что прочитали и принимаете их.
"""

# Краткая справка для окна «Правовая информация» в интерфейсе.
LEGAL_BRIEF = (
    "GITHUBCLOAD — свободное ПО под лицензией GNU AGPL-3.0-or-later.\n"
    "Проект не аффилирован с GitHub, Inc. и не поддерживается GitHub.\n\n"
    "Коротко о рисках и обязанностях:\n"
    "  • Программа поставляется «как есть», без гарантий; утрата данных, утечка токена\n"
    "    и ограничения аккаунта GitHub — риск пользователя.\n"
    "  • Пароль шифрования не восстанавливается: потеря PBEpass.txt = потеря данных.\n"
    "  • Метаданные не шифруются: имя файла/папки, размер и список частей архива\n"
    "    хранятся открытым текстом в служебном файле _gcb_manifest.json.\n"
    "  • Правила GitHub обязательны: репозитории — не хранилище и не бэкап общего\n"
    "    назначения, лимиты обходить нельзя.\n"
    "  • Запрещены нелегальный контент, вредоносное ПО, чужие данные и персональные\n"
    "    данные третьих лиц без законного основания.\n"
    "  • За загружаемые данные и соблюдение законов отвечает пользователь.\n\n"
    "Полный текст правовых предупреждений — в файле LEGAL.md, текст лицензии —\n"
    "в файле LICENSE. Оба документа есть рядом с программой, в репозитории проекта\n"
    "и в составе релиза."
)

# Пример вывода движка — используется только в режиме --smoke, чтобы проверить
# разбор и отрисовку без обращения к сети. Формат — как у GITHUBCLOAD.py.
SAMPLE_LISTING = """Найдено хранилищ GITHUBCLOAD: 2

ХРАНИЛИЩЕ «photos» (2 тома):
  ТОМ photos  (занято 12.5 МБ из ~900.0 МБ)
  - отпуск.jpg  [id 20250101_123456_ab12cd]  3.2 МБ
  - Архив документов  [id 20250102_101112_deadbe]  9.3 МБ
  ТОМ photos_2  (занято 0 Б из ~900.0 МБ)
     (пусто)

ХРАНИЛИЩЕ «docs» (1 том):
  ТОМ docs  (занято 812.4 КБ из ~900.0 МБ)
  - отчёт за март.pdf  [id 20250103_090000_cafe01]  812.4 КБ

"""


# --------------------------------------------------------------------------
# Мелкие утилиты
# --------------------------------------------------------------------------
def app_dir():
    """Папка приложения — там же, где движок ищет tokengh.txt / PBEpass.txt."""
    env_dir = os.environ.get("GCB_APP_DIR")
    if env_dir:
        return os.path.abspath(env_dir)
    if getattr(sys, "frozen", False):
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


# --------------------------------------------------------------------------
# Иконка приложения (PNG). В собранном onefile-виде app.png лежит внутри
# распакованной сборки (sys._MEIPASS), при запуске из исходников — рядом с GUI.
# Отсутствие иконки не должно ломать запуск, поэтому все ошибки глотаются.
# --------------------------------------------------------------------------
def icon_candidates():
    """Каталоги поиска иконки: распакованная сборка (_MEIPASS) → папка приложения."""
    dirs = []
    meipass = getattr(sys, "_MEIPASS", None)          # есть только у сборки PyInstaller
    if meipass:
        dirs.append(meipass)
    dirs.append(app_dir())
    unique = []
    for directory in dirs:
        if directory and directory not in unique:
            unique.append(directory)
    return unique


def find_icon():
    """Путь к PNG-иконке приложения или None, если файла нет ни в одном каталоге."""
    for directory in icon_candidates():
        path = os.path.join(directory, ICON_FILE)
        if os.path.isfile(path):
            return path
    return None


def apply_window_icon(window):
    """
    Ставит окну PNG-иконку и возвращает путь к ней (или None, если иконки нет
    либо Tk её не принял). iconphoto(True, image) делает её иконкой по умолчанию
    для всех окон приложения, включая диалоги.

    Вызывать только после создания Tk(): до этого объекта окна ещё нет.
    Ошибка или отсутствие файла не должны мешать запуску — всё в try/except.
    """
    try:
        path = find_icon()
        if not path:
            return None
        image = tk.PhotoImage(file=path)
        window.iconphoto(True, image)
    except Exception:                                     # noqa: BLE001
        return None
    # Ссылку на изображение держим в атрибуте окна: иначе сборщик мусора уничтожит
    # объект PhotoImage и иконка исчезнет.
    window._gcb_icon_image = image
    return path


# --------------------------------------------------------------------------
# Подтверждение условий использования (clickwrap): состояние — в _gcb_terms.json
# рядом с программой. Файла нет, он битый или версия условий другая — условия
# считаются непринятыми. Ошибку чтения не считаем ошибкой программы.
# --------------------------------------------------------------------------
def terms_path():
    """Путь к файлу состояния подтверждения условий (в папке приложения)."""
    return os.path.join(app_dir(), TERMS_FILE)


def terms_state():
    """Возвращает (приняты: bool, данные: dict) для текущей версии условий."""
    try:
        with open(terms_path(), "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except Exception:                                 # нет файла / битый JSON / нет прав
        return False, {}
    if not isinstance(data, dict):
        return False, {}
    return (str(data.get("terms_version") or "") == TERMS_VERSION), data


def terms_accepted():
    """True — условия актуальной версии уже приняты в этой папке приложения."""
    return terms_state()[0]


def write_terms_acceptance():
    """
    Атомарно записывает подтверждение условий (JSON: terms_version + accepted_at
    в формате ISO-8601 UTC). Возвращает (успех: bool, текст_ошибки: str).
    Недоступность записи не фатальна: вызывающий код предупреждает и продолжает.
    """
    stamp = (datetime.datetime.now(datetime.timezone.utc)
             .replace(microsecond=0).isoformat())
    payload = {"terms_version": TERMS_VERSION, "accepted_at": stamp}
    path = terms_path()
    tmp = path + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp, path)                         # атомарная подмена файла
        return True, ""
    except Exception as exc:                          # noqa: BLE001
        try:
            if os.path.exists(tmp):
                os.remove(tmp)
        except Exception:
            pass
        return False, "%s: %s" % (type(exc).__name__, exc)


# --------------------------------------------------------------------------
# Правовые документы (LEGAL.md / LICENSE): сначала файл рядом с программой,
# затем — страница файла в репозитории проекта.
# --------------------------------------------------------------------------
def document_path(filename):
    """Путь к документу рядом с программой или None, если файла нет."""
    candidates = (app_dir(), os.path.dirname(os.path.abspath(__file__)))
    for directory in candidates:
        if not directory:
            continue
        path = os.path.join(directory, filename)
        if os.path.isfile(path):
            return path
    return None


def document_url(filename):
    """Ссылка на документ в репозитории проекта (ветка main)."""
    return "%s/blob/main/%s" % (PROJECT_URL.rstrip("/"), filename)


def open_local_path(path):
    """Открывает файл приложением по умолчанию (Windows / macOS / Linux)."""
    if sys.platform.startswith("win"):
        os.startfile(path)                            # noqa: S606 — только Windows
    elif sys.platform == "darwin":
        subprocess.Popen(["open", path])
    else:
        subprocess.Popen(["xdg-open", path])


def open_document(filename):
    """
    Открывает LEGAL.md / LICENSE: сначала локальный файл рядом с программой,
    при недоступности — ссылку в репозитории проекта.
    Возвращает ("local"|"url"|"none", путь_или_ссылка).
    """
    path = document_path(filename)
    if path:
        try:
            open_local_path(path)
            return "local", path
        except Exception:                             # noqa: BLE001 — переходим к ссылке
            pass
    url = document_url(filename)
    try:
        if webbrowser.open(url):
            return "url", url
    except Exception:                                 # noqa: BLE001
        pass
    return "none", url


def write_stderr(text):
    """
    Пишет строку в stderr, если поток доступен. У GUI-сборки stderr может быть
    подавлен или отсутствовать (sys.stderr is None) — это не ошибка программы,
    поэтому пишем молча и никогда не падаем.
    """
    stream = getattr(sys, "stderr", None)
    if stream is None:
        return
    try:
        stream.write(text + "\n")
        stream.flush()
    except Exception:                                 # noqa: BLE001
        pass


def find_python():
    """Интерпретатор для сабпроцессного отката (None — если запускать нечем)."""
    if not getattr(sys, "frozen", False) and sys.executable:
        return [sys.executable]
    for name in ("python3", "python"):
        path = shutil.which(name)
        if path:
            return [path]
    return None


def pick_font(root, candidates, size, weight="normal"):
    """Шрифт с fallback: первый доступный из списка, иначе семейство по умолчанию."""
    families = set()
    try:
        families = set(tkfont.families(root))
    except Exception:
        pass
    for name in candidates:
        if name in families:
            return (name, size, weight)
    try:
        family = tkfont.nametofont("TkDefaultFont").cget("family")
    except Exception:
        family = "Helvetica"
    return (family, size, weight)


_HUMAN_RE = re.compile(r"^\s*([0-9]+(?:[.,][0-9]+)?)\s*([^\s]*)\s*$")
_HUMAN_UNITS = {
    "Б": 1, "": 1,
    "КБ": 1024, "KB": 1024,
    "МБ": 1024 ** 2, "MB": 1024 ** 2,
    "ГБ": 1024 ** 3, "GB": 1024 ** 3,
    "ТБ": 1024 ** 4, "TB": 1024 ** 4,
}


def parse_human(text):
    """«12.5 МБ» -> байты (для итогов в списке хранилищ)."""
    match = _HUMAN_RE.match(str(text if text is not None else ""))
    if not match:
        return 0
    try:
        value = float(match.group(1).replace(",", "."))
    except ValueError:
        return 0
    return int(value * _HUMAN_UNITS.get((match.group(2) or "Б").upper(), 1))


def human_bytes(num):
    num = float(num or 0)
    for unit, k in (("ТБ", 1024.0 ** 4), ("ГБ", 1024.0 ** 3),
                    ("МБ", 1024.0 ** 2), ("КБ", 1024.0)):
        if num >= k:
            return "%.1f %s" % (num / k, unit)
    return "%d Б" % int(num)


def format_args(args):
    out = []
    for arg in args:
        arg = str(arg)
        out.append('"%s"' % arg if any(ch in arg for ch in ' \t"') else arg)
    return " ".join(out)


_TRANSLIT = {
    "а": "a", "б": "b", "в": "v", "г": "g", "д": "d", "е": "e", "ё": "e", "ж": "zh",
    "з": "z", "и": "i", "й": "y", "к": "k", "л": "l", "м": "m", "н": "n", "о": "o",
    "п": "p", "р": "r", "с": "s", "т": "t", "у": "u", "ф": "f", "х": "h", "ц": "ts",
    "ч": "ch", "ш": "sh", "щ": "sch", "ъ": "", "ы": "y", "ь": "", "э": "e",
    "ю": "yu", "я": "ya",
}


def sanitize_repo_name(name):
    """Имя хранилища в виде корректного имени репозитория GitHub
    (кириллица транслитерируется, прочее заменяется на «_»)."""
    text = (name or "").strip().lower()
    out = []
    for ch in text:
        if ch in _TRANSLIT:
            out.append(_TRANSLIT[ch])
        elif ("0" <= ch <= "9") or ("a" <= ch <= "z") or ch in "._-":
            out.append(ch)
        else:
            out.append("_")
    return re.sub(r"_+", "_", "".join(out)).strip("._-")


# --------------------------------------------------------------------------
# Разбор вывода движка (формат из GITHUBCLOAD.py — его менять нельзя)
# --------------------------------------------------------------------------
RE_STORAGE_HEAD = re.compile(r"^\s*ХРАНИЛИЩЕ\s+«(?P<name>.+?)»\s*\((?P<volumes>\d+)\b")
RE_STORAGE_ONE = re.compile(r"^\s*Хранилище\s+GITHUBCLOAD\s+«(?P<name>.+?)»\s*:?\s*$")
RE_VOLUME = re.compile(
    r"^\s*ТОМ\s+(?P<repo>\S+)\s*\(занято\s+(?P<used>.+?)\s+из\s+~?(?P<total>.+?)\)\s*$")
RE_ITEM = re.compile(r"^\s*-\s+(?P<name>.+?)\s+\[id\s+(?P<id>[^\]]+)\]\s*(?P<size>.*?)\s*$")


def parse_listing(lines):
    """
    Разбирает вывод `list` / `-` / `list <имя>`:
      {имя_хранилища: {"volumes": [{"repo","used","total",
                                    "items":[{"name","id","size"}]}]}}
    """
    chains = {}

    def storage(name):
        found = chains.get(name)
        if found is None:
            found = {"volumes": []}
            chains[name] = found
        return found

    cur_storage = None
    cur_volume = None
    for raw in lines:
        line = str(raw).rstrip("\r")

        match = RE_STORAGE_HEAD.match(line) or RE_STORAGE_ONE.match(line)
        if match:
            cur_storage = storage(match.group("name"))
            cur_volume = None
            continue

        match = RE_VOLUME.match(line)
        if match:
            if cur_storage is None:
                cur_storage = storage(match.group("repo"))
            cur_volume = {"repo": match.group("repo"), "used": match.group("used"),
                          "total": match.group("total"), "items": []}
            cur_storage["volumes"].append(cur_volume)
            continue

        match = RE_ITEM.match(line)
        if match and cur_storage is not None:
            if cur_volume is None:
                cur_volume = {"repo": cur_storage.get("repo", "?"), "used": "?",
                              "total": "?", "items": []}
                cur_storage["volumes"].append(cur_volume)
            cur_volume["items"].append({"name": match.group("name"),
                                        "id": match.group("id"),
                                        "size": (match.group("size") or "").strip()})
    return chains


# --------------------------------------------------------------------------
# Вывод движка -> строки журнала
# --------------------------------------------------------------------------
class LineWriter(object):
    """Подмена sys.stdout/sys.stderr: превращает поток вывода в строки журнала."""

    encoding = "utf-8"
    errors = "replace"

    def __init__(self, emit):
        self._emit = emit
        self._buf = ""

    def write(self, data):
        if not isinstance(data, str):
            try:
                data = data.decode("utf-8", "replace")
            except Exception:
                data = str(data)
        self._buf += data
        while True:
            idx = self._buf.find("\n")
            if idx < 0:
                break
            line, self._buf = self._buf[:idx], self._buf[idx + 1:]
            self._emit(line.rstrip("\r"))
        return len(data)

    def writelines(self, lines):
        for line in lines:
            self.write(line)

    def flush(self):
        if self._buf:
            self._emit(self._buf.rstrip("\r"))
            self._buf = ""

    def reconfigure(self, **kwargs):              # движок вызывает stream.reconfigure(...)
        return None

    def isatty(self):
        return False

    def fileno(self):
        raise OSError("fileno не поддерживается: вывод идёт в журнал GUI")

    def close(self):
        self.flush()


# --------------------------------------------------------------------------
# Мост к движку
# --------------------------------------------------------------------------
class EngineRunner(object):
    """
    Вызывает движок. Основной режим — импорт в процессе и вызов main([...])
    в рабочем потоке; откат — сабпроцесс python3 GITHUBCLOAD.py ...
    """

    def __init__(self, base_dir):
        self.base_dir = base_dir
        self.mode = None                  # "inproc" | "subprocess"
        self.description = "не проверялся"
        self._module = None
        self._error = None
        self._lock = threading.Lock()

    # -- поиск движка ------------------------------------------------------
    def _candidate_paths(self):
        dirs = [os.environ.get("GCB_APP_DIR"), app_dir(),
                os.path.dirname(os.path.abspath(__file__)), os.getcwd()]
        paths = []
        for directory in dirs:
            if not directory:
                continue
            path = os.path.join(os.path.abspath(directory), ENGINE_SCRIPT)
            if path not in paths:
                paths.append(path)
        return paths

    def ensure(self):
        """Импортирует движок (один раз). Возвращает модуль или None."""
        with self._lock:
            if self._module is not None or self.mode == "subprocess":
                return self._module

            # Движок вычисляет SCRIPT_DIR так: GCB_APP_DIR -> папка exe -> папка скрипта.
            # Задаём переменную явно, чтобы tokengh.txt/PBEpass.txt искались там же,
            # где их ищет GUI (значение, заданное пользователем, не трогаем).
            os.environ.setdefault("GCB_APP_DIR", self.base_dir)

            error = None
            try:
                import GITHUBCLOAD as engine          # из исходников / из сборки
                self._module = engine
            except Exception as exc:                  # noqa: BLE001
                error = exc
                sys.modules.pop(ENGINE_MODULE, None)
                for path in self._candidate_paths():
                    if not os.path.isfile(path):
                        continue
                    try:
                        spec = importlib.util.spec_from_file_location(ENGINE_MODULE, path)
                        module = importlib.util.module_from_spec(spec)
                        sys.modules[ENGINE_MODULE] = module
                        spec.loader.exec_module(module)
                        self._module = module
                        error = None
                        break
                    except Exception as exc2:         # noqa: BLE001
                        error = exc2
                        self._module = None
                        sys.modules.pop(ENGINE_MODULE, None)

            self._error = error
            if self._module is not None:
                self.mode = "inproc"
                self.description = "в процессе: %s" % (getattr(self._module, "__file__", "?"))
            else:
                python = find_python()
                script = os.path.join(self.base_dir, ENGINE_SCRIPT)
                if python and os.path.isfile(script):
                    self.mode = "subprocess"
                    self.description = "сабпроцесс: %s %s" % (python[0], script)
                else:
                    self.mode = None
                    self.description = ("недоступен: %s"
                                        % (error or ("не найден " + ENGINE_SCRIPT)))
            return self._module

    # -- запуск ------------------------------------------------------------
    def run(self, args, emit, confirm_yes=False):
        """Синхронный вызов (из рабочего потока). Возвращает код возврата."""
        module = self.ensure()
        if module is not None:
            return self._run_inproc(module, list(args), emit, confirm_yes)
        return self._run_subprocess(list(args), emit, confirm_yes)

    def _run_inproc(self, module, args, emit, confirm_yes):
        import builtins

        out, err = LineWriter(emit), LineWriter(emit)
        saved_out, saved_err = sys.stdout, sys.stderr
        saved_input = builtins.input
        try:
            sys.stdout, sys.stderr = out, err
            if confirm_yes:
                # Диалог подтверждения уже показал GUI; движку отвечаем «yes»,
                # а его вопрос печатаем в журнал — пользователь видит, что спросили.
                def _auto_confirm(prompt=""):
                    if prompt:
                        out.write(str(prompt) + "\n")
                    return "yes"
                builtins.input = _auto_confirm
            try:
                code = module.main(list(args))
                if code is None:
                    code = 0
            except SystemExit as exc:
                code = exc.code
                if isinstance(code, str):
                    out.write(code.rstrip("\n") + "\n")
                    code = 1
                elif code is None:
                    code = 0
                else:
                    code = int(code)
            except KeyboardInterrupt:
                out.write("Прервано пользователем.\n")
                code = 130
            except BaseException as exc:              # noqa: BLE001 — движок не должен ронять GUI
                out.write("ВНУТРЕННЯЯ ОШИБКА GUI при вызове движка: %s\n" % exc)
                err.write(traceback.format_exc())
                code = 1
            return int(code)
        finally:
            try:
                out.flush()
                err.flush()
            except Exception:
                pass
            sys.stdout, sys.stderr = saved_out, saved_err
            builtins.input = saved_input

    def _run_subprocess(self, args, emit, confirm_yes):
        python = find_python()
        script = os.path.join(self.base_dir, ENGINE_SCRIPT)
        if not python or not os.path.isfile(script):
            emit("ОШИБКА: движок недоступен (%s) и запустить его сабпроцессом нельзя."
                 % (self._error or ("не найден " + ENGINE_SCRIPT)))
            return 1
        env = os.environ.copy()
        env.setdefault("GCB_APP_DIR", self.base_dir)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUNBUFFERED"] = "1"
        cmd = list(python) + [script] + [str(a) for a in args]
        emit("(откат: движок запускается сабпроцессом) %s"
             % format_args([os.path.basename(python[0]), script] + [str(a) for a in args]))
        try:
            proc = subprocess.Popen(
                cmd, cwd=self.base_dir, env=env,
                stdin=subprocess.PIPE if confirm_yes else subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                universal_newlines=True, encoding="utf-8", errors="replace",
                bufsize=1,
            )
        except Exception as exc:                      # noqa: BLE001
            emit("ОШИБКА: не удалось запустить движок: %s" % exc)
            return 1
        try:
            if confirm_yes:
                try:
                    proc.stdin.write("yes\n")
                    proc.stdin.flush()
                except Exception:
                    pass
                finally:
                    try:
                        proc.stdin.close()
                    except Exception:
                        pass
            for line in proc.stdout:
                emit(line.rstrip("\r\n"))
            proc.wait()
        finally:
            try:
                proc.stdout.close()
            except Exception:
                pass
        return int(proc.returncode or 0)


# --------------------------------------------------------------------------
# Подтверждение условий при первом запуске
# --------------------------------------------------------------------------
def show_terms_dialog(parent=None):
    """
    Модальное окно подтверждения условий использования (clickwrap) с прокручиваемым
    кратким юридическим текстом и кнопками «Принимаю условия» / «Выход».

    Возвращает True, если пользователь принял условия. Отказ, закрытие окна или Esc
    дают False — в этом случае запуск прекращается, ничего не записывается.
    Если parent не задан, создаётся собственное окно (до появления главного).
    """
    own_root = parent is None
    window = tk.Tk() if own_root else tk.Toplevel(parent)
    result = {"accepted": False}

    font_h = pick_font(window, ("DejaVu Sans", "Noto Sans", "Liberation Sans",
                                "Segoe UI", "Helvetica"), 13, "bold")
    font_base = pick_font(window, ("DejaVu Sans", "Noto Sans", "Liberation Sans",
                                   "Segoe UI", "Helvetica"), 10)
    font_small = pick_font(window, ("DejaVu Sans", "Noto Sans", "Liberation Sans",
                                    "Segoe UI", "Helvetica"), 9)

    def close():
        try:
            window.grab_release()
        except Exception:
            pass
        try:
            window.destroy()
        except Exception:
            pass

    def accept():
        result["accepted"] = True
        close()

    def decline():
        result["accepted"] = False
        close()

    try:
        window.title("GITHUBCLOAD — условия использования")
        window.minsize(560, 420)
        window.columnconfigure(0, weight=1)
        window.rowconfigure(1, weight=1)
        if not own_root:
            window.transient(parent)
        try:
            window.configure(bg=C_BG)
        except Exception:
            pass

        header = tk.Frame(window, bg=C_PANEL)
        header.grid(row=0, column=0, sticky="ew")
        tk.Label(header, text="Подтверждение условий использования", bg=C_PANEL,
                 fg=C_TEXT, font=font_h).pack(anchor="w", padx=18, pady=(14, 0))
        tk.Label(header, text="Версия условий %s · текст прокручивается" % TERMS_VERSION,
                 bg=C_PANEL, fg=C_MUTED, font=font_small).pack(anchor="w", padx=18,
                                                               pady=(2, 12))
        tk.Frame(header, bg=C_BORDER, height=1).pack(fill="x")

        body = tk.Frame(window, bg=C_BG, padx=16, pady=12)
        body.grid(row=1, column=0, sticky="nsew")
        body.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        text = tk.Text(body, wrap="word", bg=C_PANEL, fg=C_TEXT, relief="flat", bd=0,
                       highlightthickness=0, font=font_base, padx=12, pady=10,
                       height=16, spacing1=2, spacing3=2)
        bar = ttk.Scrollbar(body, orient="vertical", command=text.yview)
        text.configure(yscrollcommand=bar.set)
        text.grid(row=0, column=0, sticky="nsew")
        bar.grid(row=0, column=1, sticky="ns")
        text.insert("1.0", TERMS_TEXT)
        text.configure(state="disabled")
        text.yview_moveto(0)

        footer = tk.Frame(window, bg=C_PANEL)
        footer.grid(row=2, column=0, sticky="ew")
        tk.Frame(footer, bg=C_BORDER, height=1).pack(fill="x")
        row = tk.Frame(footer, bg=C_PANEL)
        row.pack(fill="x", padx=16, pady=12)
        tk.Label(row, text="«Выход» или закрытие окна — программа завершит работу,\n"
                           "ничего не изменяя.",
                 bg=C_PANEL, fg=C_MUTED, font=font_small,
                 justify="left").pack(side="left")
        btn_decline = ttk.Button(row, text="Выход", command=decline)
        btn_decline.pack(side="right")
        btn_accept = ttk.Button(row, text="Принимаю условия", command=accept)
        btn_accept.pack(side="right", padx=(0, 8))

        window.protocol("WM_DELETE_WINDOW", decline)
        window.bind("<Escape>", lambda event: decline())

        try:
            window.update_idletasks()
            width, height = 820, 600
            left = max(0, (window.winfo_screenwidth() - width) // 2)
            top = max(0, (window.winfo_screenheight() - height) // 3)
            window.geometry("%dx%d+%d+%d" % (width, height, left, top))
        except Exception:
            window.geometry("820x600")
        try:
            window.lift()
        except Exception:
            pass
        try:
            window.grab_set()
        except Exception:
            pass
        try:
            btn_accept.focus_set()
        except Exception:
            pass

        if own_root:
            window.mainloop()
        else:
            window.wait_window()
    except Exception:                                 # noqa: BLE001
        traceback.print_exc()
        result["accepted"] = False
    finally:
        if own_root:
            try:
                window.destroy()
            except Exception:
                pass
    return result["accepted"]


# --------------------------------------------------------------------------
# Прокручиваемая область (для списка элементов хранилища).
# Определение под if — чтобы модуль импортировался (например, для --version)
# даже там, где tkinter не установлен.
# --------------------------------------------------------------------------
if tk is not None:

    class ScrollFrame(ttk.Frame):
        def __init__(self, master):
            ttk.Frame.__init__(self, master, style="Panel.TFrame")
            self.canvas = tk.Canvas(self, bg=C_PANEL, highlightthickness=0, bd=0, takefocus=0)
            self.vbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
            self.canvas.configure(yscrollcommand=self.vbar.set)
            self.canvas.grid(row=0, column=0, sticky="nsew")
            self.vbar.grid(row=0, column=1, sticky="ns")
            self.rowconfigure(0, weight=1)
            self.columnconfigure(0, weight=1)
            self.inner = ttk.Frame(self.canvas, style="Panel.TFrame")
            self._window = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
            self.inner.bind("<Configure>", self._on_inner_configure)
            self.canvas.bind("<Configure>", self._on_canvas_configure)
            for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
                self.canvas.bind(seq, self._on_wheel, add="+")

        def _on_wheel(self, event):
            number = getattr(event, "num", None)
            if number == 4:
                delta = -1
            elif number == 5:
                delta = 1
            else:
                delta = -1 if getattr(event, "delta", 0) > 0 else 1
            self.canvas.yview_scroll(delta * 3, "units")
            return "break"

        def bind_wheel_recursive(self, widget=None):
            widget = widget or self.inner
            for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
                widget.bind(seq, self._on_wheel, add="+")
            for child in widget.winfo_children():
                self.bind_wheel_recursive(child)

        def clear(self):
            for child in self.inner.winfo_children():
                child.destroy()

        def _on_inner_configure(self, event=None):
            self.canvas.configure(scrollregion=self.canvas.bbox("all"))

        def _on_canvas_configure(self, event):
            self.canvas.itemconfigure(self._window, width=event.width)


# --------------------------------------------------------------------------
# Приложение
# --------------------------------------------------------------------------
class GcbApp(object):
    PAGE_STORAGES = "Хранилища"
    PAGE_UPLOAD = "Загрузка"
    PAGE_SELFTEST = "Самопроверка"

    def __init__(self, root, smoke=False, startup_warnings=None):
        self.root = root
        self.smoke = smoke
        self.base_dir = app_dir()

        self.queue = queue.Queue()
        self.busy = False
        self.busy_widgets = []
        self.cmd_output = []
        self.chains = {}
        self.current_storage = None
        self.item_rows = []
        self.last_dir = os.path.expanduser("~")
        self.pages = {}
        self.nav_buttons = {}
        self.current_page = None
        self.diag_label = None
        self.engine_label = None
        self.legal_window = None

        self.runner = EngineRunner(self.base_dir)

        self.root.title("GITHUBCLOAD — приватное облако на GitHub")
        self.root.geometry("1180x780")
        self.root.minsize(940, 620)
        try:
            self.root.configure(bg=C_BG)
        except Exception:
            pass

        # Иконка окна из app.png (в сборке — из _MEIPASS). Нет файла или Tk не принял
        # изображение — работаем дальше со значком по умолчанию: это не ошибка.
        try:
            self.icon_path = apply_window_icon(self.root)
        except Exception:                             # noqa: BLE001
            self.icon_path = None

        self._build_styles()
        self._build_layout()
        self.show_page(self.PAGE_STORAGES)
        # Одно короткое правовое предупреждение — один раз за запуск, в начале журнала.
        self.log(NOTICE_TEXT)
        for line in (startup_warnings or []):
            self.log(line, tag="warn")
        self._refresh_engine_info()
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.after(60, self._poll)

    # ------------------------------------------------------------------ стиль
    def _build_styles(self):
        self.style = ttk.Style(self.root)
        for theme in ("clam", "alt", "default"):
            if theme in self.style.theme_names():
                self.style.theme_use(theme)
                break

        self.font_base = pick_font(self.root, ("DejaVu Sans", "Noto Sans",
                                               "Liberation Sans", "Segoe UI",
                                               "Helvetica"), 10)
        self.font_bold = (self.font_base[0], 10, "bold")
        self.font_h1 = (self.font_base[0], 16, "bold")
        self.font_h2 = (self.font_base[0], 12, "bold")
        self.font_small = (self.font_base[0], 9)
        self.font_mono = pick_font(self.root, ("DejaVu Sans Mono", "Liberation Mono",
                                               "Noto Sans Mono", "Consolas",
                                               "Courier New"), 10)

        for named in ("TkDefaultFont", "TkTextFont", "TkMenuFont", "TkHeadingFont"):
            try:
                tkfont.nametofont(named).configure(family=self.font_base[0], size=10)
            except Exception:
                pass

        style = self.style
        style.configure("TFrame", background=C_BG)
        style.configure("Panel.TFrame", background=C_PANEL)
        style.configure("TLabel", background=C_BG, foreground=C_TEXT, font=self.font_base)
        style.configure("H1.TLabel", background=C_BG, foreground=C_TEXT, font=self.font_h1)
        style.configure("Muted.TLabel", background=C_BG, foreground=C_MUTED, font=self.font_small)
        style.configure("Panel.TLabel", background=C_PANEL, foreground=C_TEXT, font=self.font_base)
        style.configure("PanelLabel.TLabel", background=C_PANEL, foreground=C_MUTED,
                        font=self.font_small)
        style.configure("PanelH2.TLabel", background=C_PANEL, foreground=C_TEXT,
                        font=self.font_h2)
        style.configure("PanelMuted.TLabel", background=C_PANEL, foreground=C_MUTED,
                        font=self.font_small)
        style.configure("NavCap.TLabel", background=C_PANEL, foreground=C_MUTED,
                        font=self.font_small)
        style.configure("Nav.TButton", padding=(12, 8), font=self.font_base, anchor="w")
        style.configure("NavActive.TButton", padding=(12, 8), font=self.font_bold, anchor="w",
                        foreground="#ffffff", background=C_ACCENT)
        style.map("NavActive.TButton",
                  background=[("active", C_ACCENT_DARK), ("disabled", "#9db8e8")])
        style.configure("TButton", padding=(10, 5), font=self.font_base)
        style.configure("Accent.TButton", padding=(14, 6), font=self.font_bold,
                        foreground="#ffffff", background=C_ACCENT)
        style.map("Accent.TButton",
                  background=[("active", C_ACCENT_DARK), ("disabled", "#a8c0ea")],
                  foreground=[("disabled", "#f0f4ff")])
        style.configure("Danger.TButton", padding=(10, 5), font=self.font_base,
                        foreground="#ffffff", background=C_DANGER)
        style.map("Danger.TButton",
                  background=[("active", "#b23535"), ("disabled", "#e0a8a8")],
                  foreground=[("disabled", "#fff4f4")])
        style.configure("Panel.TCheckbutton", background=C_PANEL, focuscolor=C_PANEL)
        style.configure("Treeview", background=C_PANEL, fieldbackground=C_PANEL,
                        foreground=C_TEXT, rowheight=28, font=self.font_base, borderwidth=0)
        style.configure("Treeview.Heading", font=self.font_bold, background="#e7ecf5",
                        foreground=C_TEXT, relief="flat", padding=(6, 6))
        style.map("Treeview", background=[("selected", "#d8e6ff")],
                  foreground=[("selected", C_TEXT)])
        style.configure("TEntry", padding=4)

    # ---------------------------------------------------------------- каркас
    def _build_layout(self):
        outer = ttk.Frame(self.root, style="TFrame")
        outer.pack(fill="both", expand=True)
        self._build_header(outer)

        paned = ttk.Panedwindow(outer, orient="vertical")
        paned.pack(fill="both", expand=True)
        body = ttk.Frame(paned, style="TFrame", padding=(12, 10, 12, 6))
        logbox = ttk.Frame(paned, style="TFrame", padding=(12, 0, 12, 10))
        paned.add(body, weight=4)
        paned.add(logbox, weight=2)

        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)
        self._build_nav(body)

        content = ttk.Frame(body, style="TFrame")
        content.grid(row=0, column=1, sticky="nsew")
        content.rowconfigure(0, weight=1)
        content.columnconfigure(0, weight=1)
        for name, builder in ((self.PAGE_STORAGES, self._build_page_storages),
                              (self.PAGE_UPLOAD, self._build_page_upload),
                              (self.PAGE_SELFTEST, self._build_page_selftest)):
            page = builder(content)
            page.grid(row=0, column=0, sticky="nsew")
            self.pages[name] = page

        self._build_log(logbox)

    def _build_header(self, parent):
        header = tk.Frame(parent, bg=C_PANEL)
        header.pack(fill="x")
        left = tk.Frame(header, bg=C_PANEL)
        left.pack(side="left", padx=16, pady=10)
        tk.Label(left, text="GITHUBCLOAD", bg=C_PANEL, fg=C_TEXT,
                 font=self.font_h1).pack(anchor="w")
        tk.Label(left, text="приватное «облако» на GitHub · шифрование 7z AES-256 · "
                            "части по 8 МБ",
                 bg=C_PANEL, fg=C_MUTED, font=self.font_small).pack(anchor="w")
        right = tk.Frame(header, bg=C_PANEL)
        right.pack(side="right", padx=16, pady=10)
        tk.Label(right, text="GUI %s · Python %s" % (GUI_VERSION, sys.version.split()[0]),
                 bg=C_PANEL, fg=C_MUTED, font=self.font_small).pack(anchor="e")
        tk.Label(right, text=self.base_dir, bg=C_PANEL, fg=C_MUTED,
                 font=self.font_small).pack(anchor="e")
        tk.Frame(parent, bg=C_BORDER, height=1).pack(fill="x")

    def _build_nav(self, parent):
        nav = ttk.Frame(parent, style="Panel.TFrame", padding=(8, 10))
        nav.grid(row=0, column=0, sticky="nsw", padx=(0, 10))
        nav.grid_propagate(False)
        nav.configure(width=212)
        ttk.Label(nav, text="РАЗДЕЛЫ", style="NavCap.TLabel").pack(fill="x", padx=8,
                                                                   pady=(2, 8))
        for name in (self.PAGE_STORAGES, self.PAGE_UPLOAD, self.PAGE_SELFTEST):
            button = ttk.Button(nav, text=name, style="Nav.TButton",
                                command=lambda n=name: self.show_page(n))
            button.pack(fill="x", pady=3)
            self.nav_buttons[name] = button
            self._register_busy(button)
        self.engine_label = ttk.Label(nav, text="", style="NavCap.TLabel",
                                      wraplength=180, justify="left")
        self.engine_label.pack(side="bottom", fill="x", padx=8, pady=(10, 2))

    def _build_log(self, parent):
        frame = ttk.Frame(parent, style="Panel.TFrame", padding=(10, 8, 10, 10))
        frame.pack(fill="both", expand=True)

        top = ttk.Frame(frame, style="Panel.TFrame")
        top.pack(fill="x")
        ttk.Label(top, text="Журнал", style="PanelH2.TLabel").pack(side="left")
        self.autoscroll = tk.BooleanVar(value=True)
        ttk.Checkbutton(top, text="Автопрокрутка", variable=self.autoscroll,
                        style="Panel.TCheckbutton").pack(side="right", padx=(0, 12))
        ttk.Button(top, text="Очистить", command=self.clear_log).pack(side="right")
        self.progress = ttk.Progressbar(top, mode="indeterminate", length=140)
        self.progress.pack(side="right", padx=(0, 12))
        self.status_label = ttk.Label(top, text="Готово.", style="PanelMuted.TLabel")
        self.status_label.pack(side="right", padx=(0, 12))

        body = ttk.Frame(frame, style="Panel.TFrame")
        body.pack(fill="both", expand=True, pady=(6, 0))
        self.log_text = tk.Text(body, height=10, wrap="none", bg=C_LOG_BG, fg=C_LOG_FG,
                                insertbackground=C_LOG_FG, relief="flat", bd=0,
                                font=self.font_mono, padx=10, pady=8, state="disabled")
        vbar = ttk.Scrollbar(body, orient="vertical", command=self.log_text.yview)
        hbar = ttk.Scrollbar(body, orient="horizontal", command=self.log_text.xview)
        self.log_text.configure(yscrollcommand=vbar.set, xscrollcommand=hbar.set)
        self.log_text.grid(row=0, column=0, sticky="nsew")
        vbar.grid(row=0, column=1, sticky="ns")
        hbar.grid(row=1, column=0, sticky="ew")
        body.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        self.log_text.tag_configure("cmd", foreground="#7cc4ff")
        self.log_text.tag_configure("err", foreground="#ff7b72")
        self.log_text.tag_configure("warn", foreground="#ffd479")
        self.log_text.tag_configure("ok", foreground="#7ee787")
        self.log_text.tag_configure("gui", foreground="#8b949e")

    # ---------------------------------------------------------- страница 1
    def _build_page_storages(self, parent):
        page = ttk.Frame(parent, style="TFrame", padding=(14, 12))

        head = ttk.Frame(page, style="TFrame")
        head.pack(fill="x")
        ttk.Label(head, text="Хранилища", style="H1.TLabel").pack(side="left")
        ttk.Label(head, text="Двойной клик по хранилищу — открыть содержимое",
                  style="Muted.TLabel").pack(side="left", padx=(12, 0), pady=(8, 0))

        toolbar = ttk.Frame(page, style="TFrame")
        toolbar.pack(fill="x", pady=(10, 8))
        self.btn_refresh = ttk.Button(toolbar, text="Обновить", style="Accent.TButton",
                                      command=self.cmd_refresh_storages)
        self.btn_refresh.pack(side="left")
        self._register_busy(self.btn_refresh)

        self.stack = ttk.Frame(page, style="Panel.TFrame")
        self.stack.pack(fill="both", expand=True)
        self.stack.rowconfigure(0, weight=1)
        self.stack.columnconfigure(0, weight=1)
        self.view_list = self._build_storage_list(self.stack)
        self.view_detail = self._build_storage_detail(self.stack)
        for view in (self.view_list, self.view_detail):
            view.grid(row=0, column=0, sticky="nsew")
        self._show_view(self.view_list)
        return page

    def _build_storage_list(self, parent):
        frame = ttk.Frame(parent, style="Panel.TFrame", padding=10)
        columns = ("name", "volumes", "items", "size")
        self.tree_storages = ttk.Treeview(frame, columns=columns, show="headings",
                                          selectmode="browse")
        for column, title, width, anchor, stretch in (
                ("name", "Хранилище", 360, "w", True),
                ("volumes", "Томов", 90, "center", False),
                ("items", "Элементов", 110, "center", False),
                ("size", "Занято", 130, "e", False)):
            self.tree_storages.heading(column, text=title)
            self.tree_storages.column(column, width=width, anchor=anchor, stretch=stretch)
        vbar = ttk.Scrollbar(frame, orient="vertical", command=self.tree_storages.yview)
        self.tree_storages.configure(yscrollcommand=vbar.set)
        self.tree_storages.grid(row=0, column=0, sticky="nsew")
        vbar.grid(row=0, column=1, sticky="ns")
        frame.rowconfigure(0, weight=1)
        frame.columnconfigure(0, weight=1)
        self.tree_storages.bind("<Double-1>", self._on_storage_activated)
        self.tree_storages.bind("<Return>", self._on_storage_activated)

        self.storages_empty = tk.Label(
            frame,
            text="Нажмите «Обновить», чтобы получить список хранилищ с GitHub.",
            bg=C_PANEL, fg=C_MUTED, font=self.font_base)
        self.storages_empty.grid(row=0, column=0, sticky="nsew")
        return frame

    def _build_storage_detail(self, parent):
        frame = ttk.Frame(parent, style="Panel.TFrame", padding=10)

        head = ttk.Frame(frame, style="Panel.TFrame")
        head.pack(fill="x")
        self.btn_back = ttk.Button(head, text="← Хранилища", command=self.close_storage)
        self.btn_back.pack(side="left")
        self.detail_title = ttk.Label(head, text="Хранилище", style="PanelH2.TLabel")
        self.detail_title.pack(side="left", padx=12)
        self.btn_detail_refresh = ttk.Button(head, text="Обновить",
                                             command=self.cmd_refresh_detail)
        self.btn_detail_refresh.pack(side="right")
        self._register_busy(self.btn_back)
        self._register_busy(self.btn_detail_refresh)

        actions = ttk.Frame(frame, style="Panel.TFrame")
        actions.pack(fill="x", pady=(8, 6))
        self.btn_dl_selected = ttk.Button(actions, text="Скачать выбранные",
                                          command=self.cmd_download_selected)
        self.btn_dl_all = ttk.Button(actions, text="Скачать всё",
                                     command=self.cmd_download_all)
        self.btn_del_selected = ttk.Button(actions, text="Удалить выбранные (по id)",
                                           command=self.cmd_delete_selected)
        self.btn_wipe = ttk.Button(actions, text="Стереть хранилище", style="Danger.TButton",
                                   command=self.cmd_wipe)
        for button in (self.btn_dl_selected, self.btn_dl_all, self.btn_del_selected,
                       self.btn_wipe):
            button.pack(side="left", padx=(0, 8))
            self._register_busy(button)

        self.detail_summary = ttk.Label(frame, text="", style="PanelMuted.TLabel")
        self.detail_summary.pack(fill="x", pady=(0, 6))

        self.detail_scroll = ScrollFrame(frame)
        self.detail_scroll.pack(fill="both", expand=True)
        return frame

    # ---------------------------------------------------------- страница 2
    def _build_page_upload(self, parent):
        page = ttk.Frame(parent, style="TFrame", padding=(14, 12))
        ttk.Label(page, text="Загрузка", style="H1.TLabel").pack(anchor="w")
        ttk.Label(page, text="Файл или папка шифруются и уходят в приватный "
                             "репозиторий-том GitHub.",
                  style="Muted.TLabel").pack(anchor="w", pady=(2, 0))

        card = ttk.Frame(page, style="Panel.TFrame", padding=14)
        card.pack(fill="x", pady=(10, 0))
        ttk.Label(card, text="Путь к файлу или папке",
                  style="PanelLabel.TLabel").grid(row=0, column=0, sticky="w")
        self.up_path = tk.StringVar()
        entry_path = ttk.Entry(card, textvariable=self.up_path)
        entry_path.grid(row=1, column=0, sticky="we", pady=(4, 0))
        pickers = ttk.Frame(card, style="Panel.TFrame")
        pickers.grid(row=1, column=1, sticky="e", padx=(10, 0), pady=(4, 0))
        btn_file = ttk.Button(pickers, text="Выбрать файл", command=self.choose_file)
        btn_file.pack(side="left")
        btn_dir = ttk.Button(pickers, text="Выбрать папку", command=self.choose_folder)
        btn_dir.pack(side="left", padx=(6, 0))
        self.up_path_info = ttk.Label(card, text="—", style="PanelMuted.TLabel")
        self.up_path_info.grid(row=2, column=0, columnspan=2, sticky="w", pady=(6, 0))

        ttk.Label(card, text="Имя хранилища (латиница/цифры, без пробелов)",
                  style="PanelLabel.TLabel").grid(row=3, column=0, sticky="w", pady=(14, 0))
        self.up_name = tk.StringVar()
        entry_name = ttk.Entry(card, textvariable=self.up_name)
        entry_name.grid(row=4, column=0, sticky="we", pady=(4, 0))
        self.btn_upload = ttk.Button(card, text="Загрузить в облако", style="Accent.TButton",
                                     command=self.cmd_upload)
        self.btn_upload.grid(row=4, column=1, sticky="e", padx=(10, 0), pady=(4, 0))
        card.columnconfigure(0, weight=1)

        info = tk.Label(page, text=UPLOAD_INFO, bg=C_BG, fg=C_MUTED, font=self.font_small,
                        justify="left", anchor="w", wraplength=820)
        info.pack(fill="x", pady=(12, 0))
        info.bind("<Configure>", lambda event: info.configure(wraplength=max(320,
                                                                            event.width - 12)))

        for widget in (entry_path, entry_name, btn_file, btn_dir, self.btn_upload):
            self._register_busy(widget)
        entry_path.bind("<FocusOut>", lambda event: self.update_path_info())
        return page

    # ---------------------------------------------------------- страница 3
    def _build_page_selftest(self, parent):
        page = ttk.Frame(parent, style="TFrame", padding=(14, 12))
        ttk.Label(page, text="Самопроверка", style="H1.TLabel").pack(anchor="w")

        card = ttk.Frame(page, style="Panel.TFrame", padding=14)
        card.pack(fill="x", pady=(10, 0))
        text = tk.Label(card, text=SELFTEST_INFO, bg=C_PANEL, fg=C_TEXT, font=self.font_base,
                        justify="left", anchor="w", wraplength=820)
        text.pack(fill="x")
        text.bind("<Configure>", lambda event: text.configure(wraplength=max(320,
                                                                             event.width - 12)))
        self.btn_selftest = ttk.Button(card, text="Запустить самопроверку",
                                       style="Accent.TButton", command=self.cmd_selftest)
        self.btn_selftest.pack(anchor="w", pady=(12, 0))
        self._register_busy(self.btn_selftest)

        diag = ttk.Frame(page, style="Panel.TFrame", padding=14)
        diag.pack(fill="x", pady=(12, 0))
        diag_head = ttk.Frame(diag, style="Panel.TFrame")
        diag_head.pack(fill="x")
        ttk.Label(diag_head, text="Диагностика", style="PanelH2.TLabel").pack(side="left")
        self.btn_legal = ttk.Button(diag_head, text="Правовая информация",
                                    command=self.open_legal_window)
        self.btn_legal.pack(side="right")
        self._register_busy(self.btn_legal)
        self.diag_label = tk.Label(diag, text="", bg=C_PANEL, fg=C_MUTED,
                                   font=self.font_small, justify="left", anchor="w")
        self.diag_label.pack(fill="x", pady=(6, 0))
        return page

    # -------------------------------------------------- правовая информация
    def open_legal_window(self):
        """Окно «Правовая информация»: краткая справка и кнопки LEGAL.md / LICENSE."""
        existing = self.legal_window
        if existing is not None:
            try:
                if existing.winfo_exists():
                    existing.lift()
                    existing.focus_force()
                    return
            except Exception:
                pass

        window = tk.Toplevel(self.root)
        self.legal_window = window
        window.title("Правовая информация — GITHUBCLOAD")
        window.geometry("780x580")
        window.minsize(540, 400)
        window.columnconfigure(0, weight=1)
        window.rowconfigure(1, weight=1)
        try:
            window.transient(self.root)
        except Exception:
            pass

        header = tk.Frame(window, bg=C_PANEL)
        header.grid(row=0, column=0, sticky="ew")
        tk.Label(header, text="Правовая информация", bg=C_PANEL, fg=C_TEXT,
                 font=self.font_h2).pack(anchor="w", padx=16, pady=(12, 0))
        tk.Label(header, text="Версия условий %s · лицензия GNU AGPL-3.0-or-later"
                 % TERMS_VERSION,
                 bg=C_PANEL, fg=C_MUTED, font=self.font_small).pack(anchor="w", padx=16,
                                                                   pady=(2, 10))
        tk.Frame(header, bg=C_BORDER, height=1).pack(fill="x")

        body = tk.Frame(window, bg=C_BG, padx=14, pady=12)
        body.grid(row=1, column=0, sticky="nsew")
        body.rowconfigure(0, weight=1)
        body.columnconfigure(0, weight=1)
        text = tk.Text(body, wrap="word", bg=C_PANEL, fg=C_TEXT, relief="flat", bd=0,
                       highlightthickness=0, font=self.font_base, padx=12, pady=10,
                       spacing1=2, spacing3=2)
        bar = ttk.Scrollbar(body, orient="vertical", command=text.yview)
        text.configure(yscrollcommand=bar.set)
        text.grid(row=0, column=0, sticky="nsew")
        bar.grid(row=0, column=1, sticky="ns")
        text.insert("1.0", LEGAL_BRIEF)
        text.configure(state="disabled")
        text.yview_moveto(0)

        # Если документов рядом с программой нет (частая ситуация у сборки
        # dist/GITHUBCLOAD_GUI) — говорим об этом прямо и уводим в репозиторий.
        present = [name for name in (LEGAL_FILE, LICENSE_FILE) if document_path(name)]
        missing = [name for name in (LEGAL_FILE, LICENSE_FILE) if name not in present]
        if not missing:
            hint = "Файлы %s найдены рядом с программой." % ", ".join(present)
        elif present:
            hint = ("Рядом с программой есть %s; файлов %s нет — кнопки откроют их "
                    "в репозитории проекта." % (", ".join(present), ", ".join(missing)))
        else:
            hint = ("Файлов %s рядом с программой нет (так бывает у собранного файла) — "
                    "кнопки откроют документы в репозитории проекта. Полные тексты "
                    "входят в состав релиза и в репозиторий." % " и ".join((LEGAL_FILE,
                                                                          LICENSE_FILE)))
        tk.Label(window, text=hint, bg=C_BG, fg=C_MUTED, font=self.font_small,
                 justify="left", anchor="w", wraplength=720).grid(row=2, column=0,
                                                                  sticky="ew",
                                                                  padx=16, pady=(0, 6))

        footer = tk.Frame(window, bg=C_PANEL)
        footer.grid(row=3, column=0, sticky="ew")
        tk.Frame(footer, bg=C_BORDER, height=1).pack(fill="x")
        row = tk.Frame(footer, bg=C_PANEL)
        row.pack(fill="x", padx=16, pady=12)

        def open_doc(name):
            how, target = open_document(name)
            if how == "local":
                self.log("Правовая информация: открыт файл %s" % target)
                return
            if how == "url":
                self.log("Правовая информация: открыт %s в репозитории проекта" % target)
                return
            self._append_log("ВНИМАНИЕ: не удалось открыть %s — ссылка: %s"
                             % (name, target), "warn")
            try:
                self.root.clipboard_clear()
                self.root.clipboard_append(target)
                note = "Ссылка скопирована в буфер обмена."
            except Exception:
                note = "Скопируйте ссылку вручную."
            messagebox.showinfo("Правовая информация",
                                "Не удалось открыть %s автоматически.\n%s\n\n%s"
                                % (name, note, target), parent=window)

        ttk.Button(row, text="Открыть %s" % LEGAL_FILE,
                   command=lambda: open_doc(LEGAL_FILE)).pack(side="left")
        ttk.Button(row, text="Открыть %s" % LICENSE_FILE,
                   command=lambda: open_doc(LICENSE_FILE)).pack(side="left", padx=(8, 0))
        ttk.Button(row, text="Закрыть", command=self._close_legal_window).pack(side="right")
        window.protocol("WM_DELETE_WINDOW", self._close_legal_window)
        window.bind("<Escape>", lambda event: self._close_legal_window())

    def _close_legal_window(self):
        try:
            self.legal_window.destroy()
        except Exception:
            pass
        self.legal_window = None

    # --------------------------------------------------------------- журнал
    @staticmethod
    def _classify(line):
        text = line.strip()
        if not text:
            return ""
        if text.startswith("ОШИБКА") or text.startswith("ВНУТРЕННЯЯ ОШИБКА"):
            return "err"
        if text.startswith("ВНИМАНИЕ"):
            return "warn"
        if "САМОПРОВЕРКА ПРОЙДЕНА" in text or text.startswith("ГОТОВО"):
            return "ok"
        return ""

    def log(self, line, tag=None):
        """Строка журнала от самого GUI (в разбор вывода движка не попадает)."""
        self._append_log(line, tag if tag is not None else "gui")

    def _append_log(self, line, tag):
        try:
            self.log_text.configure(state="normal")
            self.log_text.insert("end", str(line) + "\n", (tag,) if tag else ())
            count = int(self.log_text.index("end-1c").split(".")[0])
            if count > MAX_LOG_LINES:
                self.log_text.delete("1.0", "%d.0" % (count - MAX_LOG_LINES + 1000))
            self.log_text.configure(state="disabled")
            if self.autoscroll.get():
                self.log_text.see("end")
        except Exception:
            pass

    def clear_log(self):
        try:
            self.log_text.configure(state="normal")
            self.log_text.delete("1.0", "end")
            self.log_text.configure(state="disabled")
        except Exception:
            pass

    def _set_status(self, text, color=None):
        try:
            self.status_label.configure(text=text)
            if color:
                self.status_label.configure(foreground=color)
        except Exception:
            pass

    # -------------------------------------------------------------- очередь
    def _emit_line(self, line):
        """Вызывается из рабочего потока движка."""
        self.queue.put(("log", line))

    def _poll(self):
        processed = 0
        try:
            while processed < 400:
                message = self.queue.get_nowait()
                processed += 1
                kind = message[0]
                if kind == "log":
                    line = message[1]
                    self._append_log(line, self._classify(line))
                    self.cmd_output.append(line)
                elif kind == "info":
                    try:
                        self.up_path_info.configure(text=message[1], foreground=C_MUTED)
                    except Exception:
                        pass
                elif kind == "done":
                    self._finish_job(message[1], message[2])
                    break
        except queue.Empty:
            pass
        try:
            self.root.after(60, self._poll)
        except Exception:
            pass

    # ----------------------------------------------------------- занятость
    def _register_busy(self, widget):
        if widget not in self.busy_widgets:
            self.busy_widgets.append(widget)

    def _set_busy(self, busy, action=None):
        self.busy = busy
        alive = []
        for widget in self.busy_widgets:
            try:
                if not widget.winfo_exists():
                    continue
            except Exception:
                continue
            alive.append(widget)
            try:
                widget.configure(state="disabled" if busy else "normal")
            except Exception:
                pass
        self.busy_widgets = alive
        try:
            if busy:
                self.progress.start(70)
                self._set_status("Выполняется: %s" % (action or "операция"), C_ACCENT)
            else:
                self.progress.stop()
        except Exception:
            pass

    # ------------------------------------------------------- запуск задания
    def start_job(self, steps, action=None, on_finish=None):
        """steps: список {"args": [...], "confirm_yes": bool}. Выполняется в потоке."""
        if self.busy:
            self.log("Операция уже выполняется — дождитесь завершения.", tag="warn")
            return
        steps = [step for step in (steps or []) if step and step.get("args")]
        if not steps:
            return
        self.cmd_output = []
        self._set_busy(True, action)
        for step in steps:
            self._append_log("$ %s" % format_args([ENGINE_SCRIPT] + list(step["args"])), "cmd")
        thread = threading.Thread(target=self._worker, args=(steps, on_finish), daemon=True)
        thread.start()

    def _worker(self, steps, on_finish):
        code = 0
        for step in steps:
            try:
                rc = self.runner.run(step["args"], self._emit_line,
                                     confirm_yes=bool(step.get("confirm_yes")))
            except Exception as exc:                  # noqa: BLE001
                self._emit_line("ВНУТРЕННЯЯ ОШИБКА GUI: %s" % exc)
                for chunk in traceback.format_exc().splitlines():
                    self._emit_line(chunk)
                rc = 1
            if rc:
                code = rc
                break
        self.queue.put(("done", code, on_finish))

    def _finish_job(self, code, on_finish):
        self._set_busy(False)
        self._append_log("— завершено, код возврата: %d" % code, "ok" if code == 0 else "err")
        self._set_status("Готово. Код возврата: %d" % code, C_OK if code == 0 else C_ERR)
        if callable(on_finish):
            try:
                on_finish(code)
            except Exception:                         # noqa: BLE001
                for chunk in traceback.format_exc().splitlines():
                    self._append_log(chunk, "err")

    # ------------------------------------------------------------ навигация
    def show_page(self, name):
        page = self.pages.get(name)
        if page is None:
            return
        page.tkraise()
        self.current_page = name
        for page_name, button in self.nav_buttons.items():
            try:
                button.configure(style="NavActive.TButton" if page_name == name
                                 else "Nav.TButton")
            except Exception:
                pass

    def _show_view(self, view):
        try:
            view.tkraise()
        except Exception:
            pass

    def _refresh_engine_info(self):
        try:
            self.runner.ensure()
        except Exception:                             # noqa: BLE001
            pass
        token = os.path.isfile(os.path.join(self.base_dir, "tokengh.txt"))
        password = os.path.isfile(os.path.join(self.base_dir, "PBEpass.txt"))
        if self.engine_label is not None:
            short = {"inproc": "в процессе",
                     "subprocess": "сабпроцесс"}.get(self.runner.mode, "недоступен")
            try:
                self.engine_label.configure(text="Движок: %s" % short)
            except Exception:
                pass
        if self.diag_label is not None:
            accepted, terms = terms_state()
            accepted_text = "да" if accepted else "нет"
            if accepted and terms.get("accepted_at"):
                accepted_text += " (%s)" % terms["accepted_at"]
            lines = [
                "Папка приложения: %s" % self.base_dir,
                "Условия приняты: %s · версия условий %s" % (accepted_text, TERMS_VERSION),
                "Движок GITHUBCLOAD.py: %s" % self.runner.description,
                "tokengh.txt: %s · PBEpass.txt: %s"
                % ("найден" if token else "НЕ найден",
                   "найден" if password else "НЕ найден"),
                "Python %s · Tk %s · платформа %s"
                % (sys.version.split()[0], getattr(tk, "TkVersion", "?"), sys.platform),
            ]
            try:
                self.diag_label.configure(text="\n".join(lines))
            except Exception:
                pass

    # ------------------------------------------------------------ хранилища
    def cmd_refresh_storages(self):
        self.current_storage = None
        self._show_view(self.view_list)
        self.start_job([{"args": ["list"]}], action="Список хранилищ",
                       on_finish=self._after_storages_list)

    def _after_storages_list(self, code):
        if code == 0:
            self._render_storages()

    def _render_storages(self):
        self.chains = parse_listing(self.cmd_output)
        tree = self.tree_storages
        for iid in tree.get_children():
            tree.delete(iid)
        for name in sorted(self.chains):
            volumes = self.chains[name]["volumes"]
            items = sum(len(volume["items"]) for volume in volumes)
            used = sum(parse_human(volume.get("used")) for volume in volumes)
            tree.insert("", "end", iid=name,
                        values=(name, len(volumes), items, human_bytes(used)))
        try:
            if self.chains:
                self.storages_empty.lower()
                self._set_status("Хранилищ: %d" % len(self.chains), C_OK)
            else:
                self.storages_empty.lift()
        except Exception:
            pass

    def _on_storage_activated(self, event=None):
        selection = self.tree_storages.selection()
        if selection:
            self.open_storage(selection[0])

    def open_storage(self, name):
        self.current_storage = name
        chain = self.chains.get(name)
        if chain:
            self._render_detail(name, chain)
        else:
            try:
                self.detail_title.configure(text="Хранилище «%s»" % name)
            except Exception:
                pass
        self._show_view(self.view_detail)
        self.cmd_refresh_detail()

    def close_storage(self):
        self.current_storage = None
        self._show_view(self.view_list)

    def cmd_refresh_detail(self):
        name = self.current_storage
        if not name:
            return
        self.start_job([{"args": ["list", name]}], action="Содержимое «%s»" % name,
                       on_finish=self._after_detail)

    def _after_detail(self, code):
        if code != 0 or not self.current_storage:
            return
        found = parse_listing(self.cmd_output)
        chain = found.get(self.current_storage)
        if chain is None:
            # имя не совпало (например, в выводе была ошибка) — берём единственное
            names = sorted(found)
            chain = found[names[0]] if len(names) == 1 else {"volumes": []}
        self.chains[self.current_storage] = chain
        self._render_detail(self.current_storage, chain)

    def _detail_label(self, text, muted=True):
        label = tk.Label(self.detail_scroll.inner, text=text, bg=C_PANEL,
                         fg=C_MUTED if muted else C_TEXT,
                         font=self.font_small if muted else self.font_base,
                         anchor="w", padx=10, pady=4)
        label.pack(fill="x")

    def _render_detail(self, name, chain):
        scroll = self.detail_scroll
        scroll.clear()
        self.item_rows = []
        volumes = chain.get("volumes") or []
        total_items = sum(len(volume["items"]) for volume in volumes)
        used = sum(parse_human(volume.get("used")) for volume in volumes)
        self.detail_title.configure(text="Хранилище «%s»" % name)
        self.detail_summary.configure(
            text="Томов: %d · элементов: %d · занято: %s"
                 % (len(volumes), total_items, human_bytes(used)))
        if not volumes:
            self._detail_label("В хранилище нет элементов — или его не удалось прочитать.")
            return
        for volume in volumes:
            header = tk.Label(scroll.inner,
                              text="ТОМ %s   (занято %s из ~%s)"
                                   % (volume["repo"], volume.get("used") or "?",
                                      volume.get("total") or "?"),
                              bg=C_ACCENT_SOFT, fg=C_TEXT, font=self.font_bold,
                              anchor="w", padx=10, pady=5)
            header.pack(fill="x", pady=(6, 4), padx=2)
            if not volume["items"]:
                self._detail_label("(пусто)")
            for item in volume["items"]:
                self._make_item_row(item, volume["repo"])
        scroll.canvas.yview_moveto(0)
        scroll.bind_wheel_recursive()
        scroll.after_idle(scroll._on_inner_configure)

    def _make_item_row(self, item, repo_name):
        row = tk.Frame(self.detail_scroll.inner, bg=C_PANEL)
        row.pack(fill="x", padx=4)
        var = tk.BooleanVar(value=False)
        check = ttk.Checkbutton(row, variable=var, style="Panel.TCheckbutton")
        check.grid(row=0, column=0, padx=(6, 6))
        name = tk.Label(row, text=item.get("name") or "?", bg=C_PANEL, fg=C_TEXT,
                        font=self.font_bold, anchor="w", justify="left")
        name.grid(row=0, column=1, sticky="we", pady=5)
        ident = tk.Label(row, text=item.get("id") or "", bg=C_PANEL, fg=C_MUTED,
                         font=self.font_small, anchor="w")
        ident.grid(row=0, column=2, sticky="w", padx=(12, 12))
        size = tk.Label(row, text=item.get("size") or "", bg=C_PANEL, fg=C_MUTED,
                        font=self.font_small, anchor="e", width=10)
        size.grid(row=0, column=3, sticky="e", padx=(0, 10))
        btn_download = ttk.Button(row, text="Скачать", width=10,
                                  command=lambda i=item: self.cmd_download_item(i))
        btn_download.grid(row=0, column=4, padx=(0, 6), pady=3)
        btn_delete = ttk.Button(row, text="Удалить", width=10,
                                command=lambda i=item: self.cmd_delete_item(i))
        btn_delete.grid(row=0, column=5, pady=3)
        row.columnconfigure(1, weight=1)
        separator = tk.Frame(self.detail_scroll.inner, bg=C_BORDER, height=1)
        separator.pack(fill="x", padx=4)

        self.item_rows.append({"item": item, "var": var, "repo": repo_name})
        self._register_busy(btn_download)
        self._register_busy(btn_delete)

    # --------------------------------------------------- операции в облаке
    def _ask_dir(self, title):
        path = filedialog.askdirectory(parent=self.root, title=title,
                                       initialdir=self.last_dir, mustexist=False)
        if path:
            self.last_dir = path
            return path
        return None

    def _selected_ids(self):
        return [row["item"]["id"] for row in self.item_rows
                if row["var"].get() and row["item"].get("id")]

    def cmd_download_item(self, item):
        if not self.current_storage:
            return
        title = "Куда скачать «%s»?" % (item.get("name") or item.get("id"))
        dest = self._ask_dir(title)
        if not dest:
            return
        self.start_job([{"args": ["download", self.current_storage, dest, item["id"]]}],
                       action="Скачивание «%s»" % (item.get("name") or item["id"]))

    def cmd_download_selected(self):
        if not self.current_storage:
            return
        ids = self._selected_ids()
        if not ids:
            messagebox.showinfo("Скачать выбранные",
                                "Отметьте файлы галочками в списке.", parent=self.root)
            return
        dest = self._ask_dir("Куда скачать отмеченные элементы (%d)?" % len(ids))
        if not dest:
            return
        self.start_job([{"args": ["download", self.current_storage, dest] + ids}],
                       action="Скачивание элементов: %d" % len(ids))

    def cmd_download_all(self):
        if not self.current_storage:
            return
        dest = self._ask_dir("Куда скачать хранилище «%s» целиком?" % self.current_storage)
        if not dest:
            return
        self.start_job([{"args": ["download", self.current_storage, dest]}],
                       action="Скачивание хранилища «%s»" % self.current_storage)

    def cmd_delete_item(self, item):
        if not self.current_storage:
            return
        question = ("Удалить элемент «%s» (id %s) из хранилища «%s»?\n\n"
                    "Данные будут стёрты из репозитория-тома без возможности отмены."
                    % (item.get("name") or "?", item.get("id") or "?", self.current_storage))
        if not messagebox.askyesno("Удаление элемента", question, icon="warning",
                                   default="no", parent=self.root):
            return
        self.start_job([{"args": ["delete", self.current_storage, item["id"]]}],
                       action="Удаление «%s»" % (item.get("name") or item["id"]),
                       on_finish=self._after_destructive)

    def cmd_delete_selected(self):
        if not self.current_storage:
            return
        ids = self._selected_ids()
        if not ids:
            messagebox.showinfo("Удалить выбранные",
                                "Отметьте файлы галочками в списке.", parent=self.root)
            return
        question = ("Удалить %d %s из хранилища «%s»?\n\n%s\n\nДействие необратимо."
                    % (len(ids), "элемент" if len(ids) == 1 else "элементов",
                       self.current_storage, "\n".join("· " + i for i in ids)))
        if not messagebox.askyesno("Удаление элементов", question, icon="warning",
                                   default="no", parent=self.root):
            return
        steps = [{"args": ["delete", self.current_storage, item_id]} for item_id in ids]
        self.start_job(steps, action="Удаление элементов: %d" % len(ids),
                       on_finish=self._after_destructive)

    def cmd_wipe(self):
        name = self.current_storage
        if not name:
            return
        question = ("БЕЗВОЗВРАТНО стереть хранилище «%s»?\n\n"
                    "Будут удалены сами репозитории-тома GitHub вместе со всеми данными.\n"
                    "Отменить это действие невозможно." % name)
        if not messagebox.askyesno("Стереть хранилище", question, icon="warning",
                                   default="no", parent=self.root):
            return
        self.start_job([{"args": ["wipe", name], "confirm_yes": True}],
                       action="Стирание хранилища «%s»" % name,
                       on_finish=self._after_wipe)

    def _after_destructive(self, code):
        if code == 0 and self.current_storage:
            self.cmd_refresh_detail()

    def _after_wipe(self, code):
        if code == 0:
            self.current_storage = None
            self._show_view(self.view_list)
            self.cmd_refresh_storages()

    # ------------------------------------------------------------- загрузка
    def choose_file(self):
        path = filedialog.askopenfilename(parent=self.root,
                                          title="Выберите файл для загрузки",
                                          initialdir=self.last_dir)
        if path:
            self.last_dir = os.path.dirname(path) or self.last_dir
            self.up_path.set(path)
            self.update_path_info(path)
            if not self.up_name.get().strip():
                self.up_name.set(sanitize_repo_name(
                    os.path.splitext(os.path.basename(path))[0]))

    def choose_folder(self):
        path = filedialog.askdirectory(parent=self.root, title="Выберите папку для загрузки",
                                       initialdir=self.last_dir)
        if path:
            self.last_dir = path
            self.up_path.set(path)
            self.update_path_info(path)
            if not self.up_name.get().strip():
                self.up_name.set(sanitize_repo_name(os.path.basename(os.path.normpath(path))))

    def update_path_info(self, path=None):
        path = (path or self.up_path.get()).strip()
        if not path:
            self.up_path_info.configure(text="—", foreground=C_MUTED)
            return
        if not os.path.exists(path):
            self.up_path_info.configure(text="путь не найден", foreground=C_ERR)
            return
        self.up_path_info.configure(text="измеряю размер…", foreground=C_MUTED)
        threading.Thread(target=self._measure_path, args=(path,), daemon=True).start()

    def _measure_path(self, path):
        try:
            if os.path.isfile(path):
                text = "файл · %s" % human_bytes(os.path.getsize(path))
            else:
                count, total = 0, 0
                for dirpath, _dirnames, filenames in os.walk(path):
                    for name in filenames:
                        count += 1
                        try:
                            total += os.path.getsize(os.path.join(dirpath, name))
                        except OSError:
                            pass
                text = "папка · файлов: %d · %s" % (count, human_bytes(total))
        except Exception as exc:                      # noqa: BLE001
            text = "не удалось измерить размер: %s" % exc
        self.queue.put(("info", text))

    def cmd_upload(self):
        path = self.up_path.get().strip()
        name = self.up_name.get().strip()
        if not path:
            messagebox.showwarning("Загрузка", "Укажите путь к файлу или папке.",
                                   parent=self.root)
            return
        if not os.path.exists(path):
            messagebox.showerror("Загрузка", "Путь не найден: %s" % path, parent=self.root)
            return
        if not name:
            messagebox.showwarning("Загрузка",
                                   "Укажите имя хранилища (например, photos).",
                                   parent=self.root)
            return
        clean = sanitize_repo_name(name)
        if clean != name:
            if not messagebox.askyesno(
                    "Загрузка",
                    "Имя «%s» не подходит для репозитория GitHub.\nИспользовать «%s»?"
                    % (name, clean or "storage"), parent=self.root):
                return
            name = clean or "storage"
            self.up_name.set(name)
        if os.path.isfile(path) and os.path.basename(path).lower() in \
                ("tokengh.txt", "pbepass.txt"):
            messagebox.showerror("Загрузка",
                                 "Этот файл служебный (tokengh.txt / PBEpass.txt) — "
                                 "движок откажется его загружать.", parent=self.root)
            return
        self.start_job([{"args": ["upload", path, name]}],
                       action="Загрузка в хранилище «%s»" % name,
                       on_finish=self._after_upload)

    def _after_upload(self, code):
        if code == 0:
            messagebox.showinfo("Загрузка", "Загрузка завершена успешно.", parent=self.root)
            self._set_status("Загрузка завершена.", C_OK)

    # -------------------------------------------------------- самопроверка
    def cmd_selftest(self):
        self.start_job([{"args": ["selftest"]}], action="Самопроверка",
                       on_finish=self._after_selftest)

    def _after_selftest(self, code):
        if code == 0:
            messagebox.showinfo("Самопроверка",
                                "САМОПРОВЕРКА ПРОЙДЕНА УСПЕШНО.\n"
                                "Шифрование, разбиение на части и расшифровка работают.",
                                parent=self.root)
            self._set_status("Самопроверка пройдена.", C_OK)

    # ------------------------------------------------------------- закрытие
    def on_close(self):
        if self.busy and not self.smoke:
            if not messagebox.askyesno("Выход", "Операция ещё выполняется. Закрыть окно?",
                                       default="no", parent=self.root):
                return
        try:
            self.root.destroy()
        except Exception:
            pass

    # ------------------------------------------------ проверка интерфейса
    def smoke_populate(self):
        """Наполняет интерфейс примером вывода движка (сеть не требуется)."""
        self.cmd_output = SAMPLE_LISTING.splitlines()
        self._render_storages()
        names = sorted(self.chains)
        if names:
            name = names[0]
            self.current_storage = name
            self._render_detail(name, self.chains[name])
            self._show_view(self.view_detail)
        self.log("$ %s list" % ENGINE_SCRIPT, tag="cmd")
        self.log("пример: обычная строка журнала")
        self.log("пример: ВНИМАНИЕ (warn)", tag="warn")
        self.log("пример: ОШИБКА (err)", tag="err")
        self.log("пример: ГОТОВО (ok)", tag="ok")


# --------------------------------------------------------------------------
# Служебный режим --smoke
# --------------------------------------------------------------------------
def smoke_run(root, app):
    root.update_idletasks()
    root.update()
    print("[smoke] окно создано: %s" % root.winfo_geometry())
    print("[smoke] иконка: %s" % (app.icon_path
                                  or "не найдена — используется значок по умолчанию"))
    for name in (app.PAGE_STORAGES, app.PAGE_UPLOAD, app.PAGE_SELFTEST):
        app.show_page(name)
        root.update_idletasks()
        root.update()
        print("[smoke] страница «%s» построена" % name)
    app.show_page(app.PAGE_STORAGES)
    app.smoke_populate()
    root.update_idletasks()
    root.update()
    current = app.chains.get(app.current_storage or "", {"volumes": []})
    print("[smoke] разбор примера вывода: хранилищ %d, элементов в «%s» %d"
          % (len(app.chains), app.current_storage or "-",
             sum(len(volume["items"]) for volume in current["volumes"])))
    print("[smoke] строк в журнале: %d" % int(app.log_text.index("end-1c").split(".")[0]))
    print("[smoke] движок: %s" % app.runner.description)
    root.update_idletasks()
    root.update()
    root.destroy()
    print("[smoke] OK — интерфейс построен, окно закрыто")
    return 0


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)

    if "--version" in args or "-V" in args:
        sys.stdout.write("%s %s\n" % (GUI_NAME, GUI_VERSION))
        return 0
    if "--help" in args or "-h" in args:
        sys.stdout.write(USAGE)
        return 0

    smoke = "--smoke" in args
    accept_terms = "--accept-terms" in args
    unknown = [arg for arg in args if arg not in ("--smoke", "--accept-terms")]
    if unknown:
        sys.stderr.write("Неизвестные аргументы: %s\n(смотрите --help)\n"
                         % " ".join(unknown))
        return 2

    # Правовое предупреждение — одна короткая строка в stderr, один раз за запуск.
    # stdout не трогаем: вывод --smoke и --version должен оставаться машинно-читаемым.
    write_stderr(NOTICE_TEXT)

    startup_warnings = []

    def record_terms_error(error):
        """Ошибка записи не фатальна: предупреждаем в журнале и продолжаем."""
        message = ("не удалось записать %s (%s) — условия подтверждены только "
                   "на этот запуск" % (terms_path(), error))
        startup_warnings.append("ВНИМАНИЕ: " + message)
        sys.stderr.write("ВНИМАНИЕ: %s\n" % message)

    # Автоматизированное принятие условий: работает и без tkinter (--accept-terms).
    if accept_terms:
        ok, error = write_terms_acceptance()
        if ok:
            sys.stderr.write("Условия использования приняты: записан %s\n" % terms_path())
        else:
            record_terms_error(error)

    if tk is None:
        sys.stderr.write(
            "ОШИБКА: tkinter недоступен (%s).\n"
            "Установите графическую библиотеку:\n"
            "  Debian/Ubuntu:  sudo apt install -y python3-tk\n"
            "  Fedora:         sudo dnf install -y python3-tkinter\n"
            "  Arch:           sudo pacman -S tk\n" % (TK_ERROR,))
        return 2

    # --- подтверждение условий: один раз на папку приложения ----------------
    # --smoke (headless-проверка) и --accept-terms окно не показывают.
    if not smoke and not accept_terms and not terms_accepted():
        if not show_terms_dialog():
            sys.stderr.write(
                "Условия использования не приняты — работа прекращена, ничего не изменено.\n"
                "Правовые предупреждения: %s, лицензия: %s.\n" % (LEGAL_FILE, LICENSE_FILE))
            return 3
        ok, error = write_terms_acceptance()
        if not ok:
            record_terms_error(error)

    try:
        root = tk.Tk()
    except Exception as exc:                          # noqa: BLE001
        sys.stderr.write("ОШИБКА: не удалось открыть окно: %s\n"
                         "Нужен графический дисплей (X11/Wayland); для headless "
                         "используйте xvfb-run.\n" % exc)
        return 3

    try:
        app = GcbApp(root, smoke=smoke, startup_warnings=startup_warnings)
        if smoke:
            return smoke_run(root, app)
        root.mainloop()
        return 0
    except Exception:                                 # noqa: BLE001
        traceback.print_exc()
        return 1
    finally:
        try:
            root.destroy()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
