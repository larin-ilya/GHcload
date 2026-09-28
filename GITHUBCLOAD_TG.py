#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GITHUBCLOAD_TG — телеграм-версия GITHUBCLOAD.

Бот-обёртка над движком GITHUBCLOAD.py (тот же формат архивов и та же логика):

  * при первом запуске просит пароль шифрования и токен GitHub, запоминает пользователя
    (данные лежат локально в data/users/<id>/, права 600);
  * любой присланный файл шифруется и заливается в приватное облако —
    по умолчанию в хранилище «telegram», имя можно задать командой /storage
    или меткой #имя в подписи к файлу;
  * по хранилищам можно ходить кнопками: список томов, список файлов, скачивание
    (файлы до 50 МБ — ограничение Bot API на отправку) и удаление элементов;
  * «Самопроверка» прогоняет selftest движка прямо из бота.

Запуск:
    python GITHUBCLOAD_TG.py --token <ТОКЕН_БОТА>

Токен также можно задать через переменную окружения GCB_TG_TOKEN
или файл token.txt рядом со скриптом.

Движок ищется рядом: GITHUBCLOAD.py.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import html
import json
import logging
import os
import re
import shutil
import sys
import tempfile
import time
import zipfile
from datetime import datetime
from pathlib import Path

from aiogram import Bot, Dispatcher, F, Router
from aiogram.client.default import DefaultBotProperties
from aiogram.client.session.aiohttp import AiohttpSession
from aiogram.enums import ChatAction, ParseMode
from aiogram.exceptions import TelegramBadRequest, TelegramForbiddenError, TelegramRetryAfter
from aiogram.filters import Command, CommandStart
from aiogram.fsm.context import FSMContext
from aiogram.fsm.state import State, StatesGroup
from aiogram.fsm.storage.base import BaseStorage, StorageKey
from aiogram.types import (
    BotCommand,
    BotCommandScopeDefault,
    CallbackQuery,
    FSInputFile,
    InlineKeyboardButton,
    InlineKeyboardMarkup,
    KeyboardButton,
    Message,
    ReplyKeyboardMarkup,
)

# --------------------------------------------------------------------------
# Версия и константы
# --------------------------------------------------------------------------
TG_VERSION = "1.0.0"

ENGINE_NAME = "GITHUBCLOAD.py"

# Ограничения Bot API (core.telegram.org/bots/api):
#   * getFile — бот может скачать файл не больше 20 МБ;
#   * sendDocument — бот может отправить файл не больше 50 МБ.
TG_INGEST_LIMIT = 20 * 1024 * 1024
TG_SEND_LIMIT = 50 * 1024 * 1024

DEFAULT_STORAGE = "telegram"
PAGE_SIZE = 8
ENGINE_TIMEOUT = 900          # секунд на одну операцию движка

BASE_DIR = Path(__file__).resolve().parent
ENGINE = BASE_DIR / ENGINE_NAME
DATA_DIR = BASE_DIR / "data"
USERS_DIR = DATA_DIR / "users"
LOG_FILE = DATA_DIR / "bot.log"

NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,38}$")
CAPTION_STORE_RE = re.compile(r"#([A-Za-z0-9][A-Za-z0-9._-]{0,38})")

log = logging.getLogger("gcb-tg")

# --------------------------------------------------------------------------
# Утилиты: пути пользователя, состояние
# --------------------------------------------------------------------------
def user_dir(uid: int) -> Path:
    return USERS_DIR / str(uid)


def user_app_dir(uid: int) -> Path:
    """Каталог приложения для движка (GCB_APP_DIR): tokengh.txt + PBEpass.txt."""
    return user_dir(uid) / "app"


def user_meta_path(uid: int) -> Path:
    return user_dir(uid) / "meta.json"


def user_work_dir(uid: int) -> Path:
    d = user_dir(uid) / "work"
    d.mkdir(parents=True, exist_ok=True)
    return d


def load_meta(uid: int) -> dict:
    p = user_meta_path(uid)
    if not p.is_file():
        return {}
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except Exception:
        return {}


def save_meta(uid: int, meta: dict) -> None:
    user_dir(uid).mkdir(parents=True, exist_ok=True)
    tmp = user_meta_path(uid).with_suffix(".tmp")
    tmp.write_text(json.dumps(meta, ensure_ascii=False, indent=1), encoding="utf-8")
    os.replace(tmp, user_meta_path(uid))


def is_registered(uid: int) -> bool:
    app = user_app_dir(uid)
    if not (app / "tokengh.txt").is_file() or not (app / "PBEpass.txt").is_file():
        return False
    return bool(load_meta(uid).get("registered"))


def write_credentials(uid: int, token: str, password: str) -> None:
    app = user_app_dir(uid)
    app.mkdir(parents=True, exist_ok=True)
    try:
        os.chmod(user_dir(uid), 0o700)
        os.chmod(app, 0o700)
    except OSError:
        pass
    (app / "tokengh.txt").write_text(token + "\n", encoding="utf-8")
    (app / "PBEpass.txt").write_text(password + "\n", encoding="utf-8")
    for name in ("tokengh.txt", "PBEpass.txt"):
        try:
            os.chmod(app / name, 0o600)
        except OSError:
            pass
    meta = load_meta(uid)
    meta.setdefault("current_storage", DEFAULT_STORAGE)
    meta["registered"] = True
    meta["registered_at"] = datetime.now().isoformat(timespec="seconds")
    save_meta(uid, meta)


def forget_user(uid: int) -> None:
    d = user_dir(uid)
    if d.is_dir():
        shutil.rmtree(d, ignore_errors=True)


def get_current_storage(uid: int) -> str:
    name = (load_meta(uid).get("current_storage") or DEFAULT_STORAGE).strip()
    return name if NAME_RE.match(name) else DEFAULT_STORAGE


def set_current_storage(uid: int, name: str) -> None:
    meta = load_meta(uid)
    meta["current_storage"] = name
    save_meta(uid, meta)


# --------------------------------------------------------------------------
# Запуск движка
# --------------------------------------------------------------------------
async def run_engine(uid: int, args: list[str], timeout: float = ENGINE_TIMEOUT,
                     stdin_data: str | None = None) -> tuple[int, str]:
    """Запускает GITHUBCLOAD.py с кредами пользователя. Возвращает (код, вывод).

    stdin_data нужен для команд, спрашивающих подтверждение (wipe — «yes»).
    Без него stdin закрыт: движок не может «зависнуть» на вопросе.
    """
    if not ENGINE.is_file():
        return 2, "НЕ НАЙДЕН ДВИЖОК %s" % ENGINE
    env = os.environ.copy()
    env["GCB_APP_DIR"] = str(user_app_dir(uid))
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONUTF8"] = "1"
    proc = await asyncio.create_subprocess_exec(
        sys.executable, str(ENGINE), *args,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.STDOUT,
        stdin=(asyncio.subprocess.PIPE if stdin_data is not None
               else asyncio.subprocess.DEVNULL),
        cwd=str(BASE_DIR),
        env=env,
    )
    try:
        out, _ = await asyncio.wait_for(
            proc.communicate(stdin_data.encode("utf-8") if stdin_data is not None else None),
            timeout=timeout)
    except asyncio.TimeoutError:
        with contextlib.suppress(Exception):
            proc.kill()
        return 124, "⏱ Операция не уложилась в %d сек и была прервана." % int(timeout)
    text = (out or b"").decode("utf-8", "replace").strip()
    return (proc.returncode or 0), text


# --------------------------------------------------------------------------
# Разбор вывода движка
# --------------------------------------------------------------------------
RE_STORAGE_HEAD = re.compile(r"^ХРАНИЛИЩЕ «(.+?)»\s*\((\d+)\s+том")
RE_STORAGE_ONE = re.compile(r"^Хранилище GITHUBCLOAD «(.+?)»:")
RE_VOLUME = re.compile(r"^\s*ТОМ (\S+)\s+\(занято (.+?) из ~(.+?)\)\s*$")
RE_ITEM = re.compile(r"^\s*- (.+?)\s\s+\[id (\S+)\]\s\s*(.*)$")
RE_FOUND = re.compile(r"^Найдено хранилищ GITHUBCLOAD: (\d+)$")


def parse_storages(text: str) -> dict[str, dict]:
    """Разбирает вывод `list` -> {имя: {"volumes": n, "items": [...]}}."""
    out: dict[str, dict] = {}
    cur = None
    for line in text.splitlines():
        m = RE_STORAGE_HEAD.match(line) or RE_STORAGE_ONE.match(line)
        if m:
            cur = m.group(1)
            out.setdefault(cur, {"volumes": 0, "items": []})
            if line.startswith("ХРАНИЛИЩЕ"):
                out[cur]["volumes"] = int(m.group(2))
            continue
        if cur is None:
            continue
        if RE_VOLUME.match(line):
            out[cur]["volumes"] = max(out[cur]["volumes"], 1)
            continue
        mi = RE_ITEM.match(line)
        if mi:
            out[cur]["items"].append({
                "name": mi.group(1).strip(),
                "id": mi.group(2),
                "size": (mi.group(3) or "").strip(),
            })
    return out


def parse_items(text: str) -> list[dict]:
    items = []
    seen = set()
    for line in text.splitlines():
        mi = RE_ITEM.match(line)
        if mi:
            iid = mi.group(2)
            if iid in seen:
                continue
            seen.add(iid)
            items.append({
                "name": mi.group(1).strip(),
                "id": iid,
                "size": (mi.group(3) or "").strip(),
            })
    return items


def human_to_bytes(s: str) -> int:
    """«3.2 МБ» -> байты (грубо, для предварительной проверки лимитов)."""
    m = re.match(r"^\s*([\d.,]+)\s*([КМГТ]?Б|Б)?\s*$", s or "")
    if not m:
        return 0
    val = float(m.group(1).replace(",", "."))
    unit = (m.group(2) or "Б").upper()
    mult = {"Б": 1, "КБ": 1024, "МБ": 1024 ** 2, "ГБ": 1024 ** 3, "ТБ": 1024 ** 4}
    return int(val * mult.get(unit, 1))


def esc(s: str) -> str:
    return html.escape(str(s), quote=False)


# --------------------------------------------------------------------------
# Тексты
# --------------------------------------------------------------------------
WELCOME = (
    "👋 <b>GITHUBCLOAD</b> — приватное облако на GitHub.\n\n"
    "Перед началом нужно один раз ввести доступы — они сохранятся "
    "только на этом сервере, рядом с ботом:\n\n"
    "1️⃣ <b>пароль шифрования</b> (тот же, что в <code>PBEpass.txt</code> "
    "на ваших других устройствах);\n"
    "2️⃣ <b>токен GitHub</b> (scope <code>repo</code>, при желании "
    "<code>delete_repo</code>).\n\n"
    "🔐 Присланные сообщения бот удалит из чата сразу после сохранения. "
    "Потеря пароля = потеря данных: он нигде не восстанавливается.\n\n"
    "Шаг 1 из 2 — пришлите <b>пароль шифрования</b> одним сообщением."
)

ASK_TOKEN = (
    "2️⃣ Шаг 2 из 2 — пришлите <b>токен GitHub</b> одним сообщением.\n"
    "Подойдёт классический токен (<code>ghp_…</code>) со scope <code>repo</code>."
)

HELP = (
    "ℹ️ <b>Как пользоваться</b>\n\n"
    "📥 <b>Загрузка в облако</b> — просто отправьте боту файл (документ, фото, "
    "видео, аудио, голосовое). Бот зашифрует его (7z, AES-256) и зальёт "
    "в приватный репозиторий GitHub.\n"
    "• по умолчанию — в хранилище «<b>telegram</b>»;\n"
    "• другое хранилище: команда <code>/storage имя</code> или метка "
    "<code>#имя</code> в подписи к файлу.\n\n"
    "📦 <b>Браузер облака</b> — <code>/storages</code> или кнопка «Хранилища»: "
    "список хранилищ → файлы → скачать или удалить.\n\n"
    "📤 <b>Скачивание из облака</b> — кнопка «⬇️» у файла. Отправляются файлы "
    "до <b>50 МБ</b> (ограничение Bot API на отправку).\n\n"
    "⚠️ Ограничение приёма: Telegram Bot API не даёт боту скачать присланный "
    "файл больше <b>20 МБ</b> — такие файлы заливайте через GUI/CLI.\n\n"
    "Команды: /start, /storages, /storage, /selftest, /status, /logout, /help"
)


def main_menu_kb() -> ReplyKeyboardMarkup:
    return ReplyKeyboardMarkup(
        keyboard=[
            [KeyboardButton(text="📦 Хранилища"), KeyboardButton(text="⚙️ Настройки")],
            [KeyboardButton(text="♻️ Самопроверка"), KeyboardButton(text="ℹ️ Помощь")],
        ],
        resize_keyboard=True,
    )


def storages_kb(storages: dict[str, dict]) -> InlineKeyboardMarkup:
    rows = []
    for name, info in sorted(storages.items()):
        cnt = len(info.get("items") or [])
        rows.append([InlineKeyboardButton(
            text="📦 %s · %d файл(ов)" % (name, cnt),
            callback_data="s|%s|0" % name,
        )])
    if not rows:
        rows.append([InlineKeyboardButton(text="— пусто —", callback_data="noop")])
    rows.append([InlineKeyboardButton(text="🔄 Обновить", callback_data="storages")])
    return InlineKeyboardMarkup(inline_keyboard=rows)


def items_kb(store: str, items: list[dict], page: int) -> InlineKeyboardMarkup:
    pages = max(1, (len(items) + PAGE_SIZE - 1) // PAGE_SIZE)
    page = max(0, min(page, pages - 1))
    chunk = items[page * PAGE_SIZE:(page + 1) * PAGE_SIZE]
    rows = []
    for it in chunk:
        rows.append([
            InlineKeyboardButton(
                text="⬇️ %s · %s" % (it["name"][:28], it["size"] or "?"),
                callback_data="d|%s|%s" % (store, it["id"]),
            ),
            InlineKeyboardButton(text="🗑", callback_data="x|%s|%s" % (store, it["id"])),
        ])
    nav = []
    if page > 0:
        nav.append(InlineKeyboardButton(text="◀️", callback_data="s|%s|%d" % (store, page - 1)))
    nav.append(InlineKeyboardButton(text="%d/%d" % (page + 1, pages), callback_data="noop"))
    if page < pages - 1:
        nav.append(InlineKeyboardButton(text="▶️", callback_data="s|%s|%d" % (store, page + 1)))
    rows.append(nav)
    rows.append([
        InlineKeyboardButton(text="🔄 Обновить", callback_data="s|%s|%d" % (store, page)),
        InlineKeyboardButton(text="📦 К хранилищам", callback_data="storages"),
    ])
    return InlineKeyboardMarkup(inline_keyboard=rows)


def confirm_kb(action: str) -> InlineKeyboardMarkup:
    return InlineKeyboardMarkup(inline_keyboard=[[
        InlineKeyboardButton(text="✅ Да", callback_data="yes|" + action),
        InlineKeyboardButton(text="✖️ Нет", callback_data="noop"),
    ]])


# --------------------------------------------------------------------------
# Бот
# --------------------------------------------------------------------------
class JsonFileStorage(BaseStorage):
    """Простейшее файловое хранилище состояний FSM — переживает перезапуск бота."""

    def __init__(self, path: Path) -> None:
        self._path = path
        self._data: dict[str, dict] = {}
        try:
            self._data = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            self._data = {}

    def _flush(self) -> None:
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self._path.with_suffix(".tmp")
            tmp.write_text(json.dumps(self._data, ensure_ascii=False), encoding="utf-8")
            os.replace(tmp, self._path)
        except Exception:
            log.exception("fsm: не удалось сохранить состояния")

    @staticmethod
    def _k(key: StorageKey) -> str:
        return "%s:%s:%s" % (key.bot_id, key.chat_id, key.user_id)

    async def set_state(self, key: StorageKey, state=None) -> None:  # type: ignore[override]
        rec = self._data.setdefault(self._k(key), {})
        rec["state"] = state.state if hasattr(state, "state") else state
        self._flush()

    async def get_state(self, key: StorageKey):  # type: ignore[override]
        rec = self._data.get(self._k(key)) or {}
        return rec.get("state")

    async def set_data(self, key: StorageKey, data: dict) -> None:  # type: ignore[override]
        rec = self._data.setdefault(self._k(key), {})
        rec["data"] = dict(data)
        self._flush()

    async def get_data(self, key: StorageKey) -> dict:  # type: ignore[override]
        rec = self._data.get(self._k(key)) or {}
        return dict(rec.get("data") or {})

    async def close(self) -> None:  # type: ignore[override]
        self._flush()


router = Router()
dp = Dispatcher(storage=JsonFileStorage(DATA_DIR / "fsm.json"))
dp.include_router(router)

# анти-флуд на пользователя: одна тяжёлая операция за раз
user_locks: dict[int, asyncio.Lock] = {}


def lock_for(uid: int) -> asyncio.Lock:
    if uid not in user_locks:
        user_locks[uid] = asyncio.Lock()
    return user_locks[uid]


class Reg(StatesGroup):
    password = State()
    token = State()


def allowed(uid: int) -> bool:
    allow = os.environ.get("GCB_TG_ALLOWED", "").strip()
    if not allow:
        return True
    return str(uid) in {x.strip() for x in allow.split(",") if x.strip()}


async def soft_delete(message: Message) -> None:
    try:
        await message.delete()
    except Exception:
        pass


async def deny_if_needed(message: Message) -> bool:
    uid = message.from_user.id
    if not allowed(uid):
        await message.answer("⛔️ Доступ к этому боту ограничен.")
        return True
    if not is_registered(uid):
        await message.answer(
            "Сначала пройдите регистрацию: /start",
            reply_markup=main_menu_kb(),
        )
        return True
    return False


# ---------------------------------------------------------------- старт
@router.message(CommandStart())
async def cmd_start(message: Message, state: FSMContext) -> None:
    uid = message.from_user.id
    log.info("reg: /start от uid=%s (зарегистрирован: %s)", uid, is_registered(uid))
    if not allowed(uid):
        await message.answer("⛔️ Доступ к этому боту ограничен.")
        return
    if is_registered(uid):
        await state.clear()
        store = get_current_storage(uid)
        await message.answer(
            "✅ С возвращением! Текущее хранилище: <b>%s</b>\n"
            "Отправьте файл — залью в облако, или откройте «📦 Хранилища»." % esc(store),
            reply_markup=main_menu_kb(),
        )
        return
    await state.set_state(Reg.password)
    await message.answer(WELCOME)


@router.message(Reg.password, F.text, ~F.text.startswith("/"))
async def reg_password(message: Message, state: FSMContext) -> None:
    uid = message.from_user.id
    password = (message.text or "").strip()
    await soft_delete(message)
    log.info("reg: пароль получен от uid=%s (длина %d)", uid, len(password))
    if not password:
        await message.answer("Пароль пустой — пришлите ещё раз.")
        return
    data = await state.get_data()
    data["password"] = password
    await state.update_data(**data)
    await state.set_state(Reg.token)
    await message.answer(ASK_TOKEN)


@router.message(Reg.token, F.text, ~F.text.startswith("/"))
async def reg_token(message: Message, state: FSMContext) -> None:
    uid = message.from_user.id
    token = (message.text or "").strip()
    await soft_delete(message)
    log.info("reg: токен получен от uid=%s (длина %d)", uid, len(token))
    if not re.match(r"^(ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|[A-Za-z0-9_]{30,})$", token):
        await message.answer(
            "Это не похоже на токен GitHub. Пришлите токен ещё раз "
            "(например, <code>ghp_…</code>) или /cancel."
        )
        return
    data = await state.get_data()
    password = data.get("password") or ""
    write_credentials(uid, token, password)
    ok, out = await run_engine(uid, ["list"], timeout=300)
    log.info("reg: проверка токена uid=%s → код %s", uid, ok)
    if ok != 0:
        forget_user(uid)
        hint = ""
        if "401" in out or "Bad credentials" in out:
            hint = "\nТокен не принят GitHub (401)."
        await state.set_state(Reg.token)
        log.warning("reg: токен не подтверждён uid=%s: %s", uid, out[:300].replace("\n", " | "))
        await message.answer(
            "❌ Не удалось подключиться к GitHub.%s\n\n%s\n\n"
            "Пришлите токен ещё раз или /cancel." % (hint, esc(out[:500]))
        )
        return
    await state.clear()
    store = get_current_storage(uid)
    found = RE_FOUND.search(out)
    n = found.group(1) if found else "?"
    log.info("reg: uid=%s зарегистрирован, хранилищ: %s", uid, n)
    await message.answer(
        "✅ <b>Готово!</b> Доступы сохранены, GitHub отвечает.\n"
        "Хранилищ в аккаунте: <b>%s</b>\n"
        "Текущее хранилище для загрузок: <b>%s</b>\n\n"
        "Просто отправьте файл — залью в облако." % (n, esc(store)),
        reply_markup=main_menu_kb(),
    )


@router.message(Command("cancel"))
async def cmd_cancel(message: Message, state: FSMContext) -> None:
    await state.clear()
    await message.answer("Ок, отменил.", reply_markup=main_menu_kb())


@router.message(Command("help"))
@router.message(F.text == "ℹ️ Помощь")
async def cmd_help(message: Message) -> None:
    await message.answer(HELP, reply_markup=main_menu_kb())


# ------------------------------------------------------- список хранилищ
async def render_storages(target: Message, uid: int, edit: bool = False) -> None:
    async with lock_for(uid):
        code, out = await run_engine(uid, ["list"])
    if code != 0:
        text = "❌ Не удалось получить список хранилищ:\n<pre>%s</pre>" % esc(out[:800])
        if edit:
            await target.edit_text(text)
        else:
            await target.answer(text)
        return
    storages = parse_storages(out)
    if not storages:
        text = ("📦 Хранилищ пока нет.\nОтправьте файл — создам хранилище "
                "«<b>%s</b>» автоматически." % esc(get_current_storage(uid)))
    else:
        total = sum(len(s.get("items") or []) for s in storages.values())
        text = ("📦 <b>Хранилища</b> (%d шт., файлов: %d)\n"
                "Выберите хранилище:" % (len(storages), total))
    kb = storages_kb(storages)
    if edit:
        try:
            await target.edit_text(text, reply_markup=kb)
        except TelegramBadRequest:
            await target.answer(text, reply_markup=kb)
    else:
        await target.answer(text, reply_markup=kb)


@router.message(Command("storages"))
@router.message(F.text == "📦 Хранилища")
async def cmd_storages(message: Message) -> None:
    if await deny_if_needed(message):
        return
    await render_storages(message, message.from_user.id)


@router.callback_query(F.data == "storages")
async def cb_storages(cq: CallbackQuery) -> None:
    if not allowed(cq.from_user.id) or not is_registered(cq.from_user.id):
        await cq.answer("Сначала /start", show_alert=True)
        return
    await cq.answer()
    await render_storages(cq.message, cq.from_user.id, edit=True)


async def render_store(target: Message, uid: int, store: str, page: int, edit: bool = True) -> None:
    async with lock_for(uid):
        code, out = await run_engine(uid, ["list", store])
    if code != 0:
        text = "❌ Не удалось прочитать хранилище «%s»:\n<pre>%s</pre>" % (esc(store), esc(out[:600]))
        kb = None
    else:
        items = parse_items(out)
        if not items:
            text = "📦 Хранилище «<b>%s</b>» пусто.\nОтправьте файл — залью сюда." % esc(store)
            kb = InlineKeyboardMarkup(inline_keyboard=[[
                InlineKeyboardButton(text="📦 К хранилищам", callback_data="storages")]])
        else:
            total_bytes = sum(human_to_bytes(i["size"]) for i in items)
            text = ("📦 Хранилище «<b>%s</b>» — файлов: <b>%d</b>, объём: ~%s\n"
                    "Нажмите ⬇️, чтобы скачать, или 🗑, чтобы удалить." %
                    (esc(store), len(items), esc(human_size(total_bytes))))
            kb = items_kb(store, items, page)
    if edit:
        try:
            await target.edit_text(text, reply_markup=kb)
            return
        except TelegramBadRequest:
            pass
    await target.answer(text, reply_markup=kb)


def human_size(n: int) -> str:
    if n <= 0:
        return "0 Б"
    units = ["Б", "КБ", "МБ", "ГБ", "ТБ"]
    v = float(n)
    i = 0
    while v >= 1024 and i < len(units) - 1:
        v /= 1024.0
        i += 1
    return ("%d %s" % (round(v), units[i])) if i == 0 else ("%.1f %s" % (v, units[i]))


@router.callback_query(F.data.startswith("s|"))
async def cb_store(cq: CallbackQuery) -> None:
    uid = cq.from_user.id
    if not allowed(uid) or not is_registered(uid):
        await cq.answer("Сначала /start", show_alert=True)
        return
    parts = cq.data.split("|")
    store, page = parts[1], int(parts[2]) if len(parts) > 2 else 0
    await cq.answer()
    await render_store(cq.message, uid, store, page)


# ------------------------------------------------------------- настройки
@router.message(Command("storage"))
@router.message(F.text == "⚙️ Настройки")
async def cmd_storage(message: Message) -> None:
    if await deny_if_needed(message):
        return
    uid = message.from_user.id
    cur = get_current_storage(uid)
    arg = ""
    if message.text and message.text.startswith("/storage"):
        arg = message.text[len("/storage"):].strip()
    if arg:
        if arg.lower() in ("reset", "default", "сброс"):
            set_current_storage(uid, DEFAULT_STORAGE)
            await message.answer("Текущее хранилище сброшено на «<b>%s</b>»." % DEFAULT_STORAGE)
            return
        if not NAME_RE.match(arg):
            await message.answer(
                "Имя хранилища — латиница/цифры, до 39 символов "
                "(например, <code>photos</code>). Попробуйте ещё раз.")
            return
        set_current_storage(uid, arg)
        await message.answer("✅ Текущее хранилище: <b>%s</b>" % esc(arg))
        return
    await message.answer(
        "⚙️ <b>Настройки</b>\n\n"
        "Текущее хранилище для загрузок: <b>%s</b>\n\n"
        "• сменить: <code>/storage имя</code>\n"
        "• сбросить: <code>/storage reset</code>\n"
        "• проверить движок: /selftest\n"
        "• удалить свои доступы: /logout" % esc(cur),
        reply_markup=main_menu_kb(),
    )


@router.message(Command("status"))
async def cmd_status(message: Message) -> None:
    if await deny_if_needed(message):
        return
    uid = message.from_user.id
    meta = load_meta(uid)
    await message.answer(
        "📊 <b>Статус</b>\n"
        "• пользователь: <code>%d</code> (%s)\n"
        "• доступы сохранены: %s\n"
        "• текущее хранилище: <b>%s</b>\n"
        "• зарегистрирован: %s\n"
        "• бот: GITHUBCLOAD_TG %s" % (
            uid,
            esc(message.from_user.full_name),
            "да" if is_registered(uid) else "нет",
            esc(get_current_storage(uid)),
            esc(meta.get("registered_at", "—")),
            TG_VERSION,
        ),
        reply_markup=main_menu_kb(),
    )


@router.message(Command("logout"))
async def cmd_logout(message: Message) -> None:
    if await deny_if_needed(message):
        return
    await message.answer(
        "🗑 Забыть ваши доступы (токен и пароль) и настройки?\n"
        "Данные в облаке останутся на месте — удаляются только локальные доступы.",
        reply_markup=confirm_kb("logout"),
    )


@router.message(Command("selftest"))
@router.message(F.text == "♻️ Самопроверка")
async def cmd_selftest(message: Message) -> None:
    if await deny_if_needed(message):
        return
    uid = message.from_user.id
    note = await message.answer("♻️ Запускаю самопроверку движка (без GitHub)…")
    async with lock_for(uid):
        code, out = await run_engine(uid, ["selftest"], timeout=600)
    ok = "САМОПРОВЕРКА ПРОЙДЕНА" in out
    head = "✅ <b>Самопроверка пройдена</b>" if ok else "❌ <b>Самопроверка не прошла</b>"
    await note.edit_text("%s\n\n<pre>%s</pre>" % (head, esc(out[-1500:])))


# ------------------------------------------------------- приём файлов
def pick_file(message: Message) -> tuple[str, str, int] | None:
    """Возвращает (file_id, имя, размер) для поддерживаемых типов сообщений."""
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    if message.document:
        d = message.document
        return d.file_id, (d.file_name or ("document_%s.bin" % ts)), int(d.file_size or 0)
    if message.video:
        v = message.video
        return v.file_id, (v.file_name or ("video_%s.mp4" % ts)), int(v.file_size or 0)
    if message.audio:
        a = message.audio
        return a.file_id, (a.file_name or ("audio_%s.mp3" % ts)), int(a.file_size or 0)
    if message.voice:
        v = message.voice
        return v.file_id, ("voice_%s.ogg" % ts), int(v.file_size or 0)
    if message.video_note:
        v = message.video_note
        return v.file_id, ("video_note_%s.mp4" % ts), int(v.file_size or 0)
    if message.photo:
        p = message.photo[-1]
        return p.file_id, ("photo_%s.jpg" % ts), int(p.file_size or 0)
    return None


def safe_name(name: str) -> str:
    name = os.path.basename(name.replace("\\", "/")).strip() or "file.bin"
    name = re.sub(r"[\x00-\x1f<>:\"|?*]", "_", name)
    return name[:180]


@router.message(F.document | F.video | F.audio | F.voice | F.video_note | F.photo)
async def on_file(message: Message, bot: Bot) -> None:
    if await deny_if_needed(message):
        return
    uid = message.from_user.id
    picked = pick_file(message)
    if not picked:
        return
    file_id, raw_name, size = picked
    name = safe_name(raw_name)
    caption = message.caption or ""
    m = CAPTION_STORE_RE.search(caption)
    store = m.group(1) if m else get_current_storage(uid)

    log.info("file: uid=%s «%s» %d Б → хранилище «%s»", uid, name, size, store)

    if size and size > TG_INGEST_LIMIT:
        await message.answer(
            "⚠️ Файл <b>%s</b> — %s.\n"
            "Telegram Bot API не даёт боту скачать файл больше <b>20 МБ</b> "
            "(ограничение <code>getFile</code>).\n\n"
            "Варианты: залейте через GUI/CLI либо разбейте файл на части." %
            (esc(name), human_size(size)),
            reply_markup=main_menu_kb(),
        )
        return

    work = user_work_dir(uid)
    dest = work / ("%d_%s" % (int(time.time()), name))
    status = await message.answer(
        "⏳ Принял <b>%s</b> (%s).\nСкачиваю из Telegram…" % (esc(name), human_size(size) or "?"))
    try:
        await bot.send_chat_action(message.chat.id, ChatAction.TYPING)
        await bot.download(file_id, destination=str(dest), timeout=600)
    except Exception as exc:
        await status.edit_text("❌ Не удалось скачать файл из Telegram: %s" % esc(str(exc)))
        return

    real_size = dest.stat().st_size if dest.exists() else 0
    await status.edit_text(
        "🔐 Шифрую и заливаю <b>%s</b> (%s) в хранилище «<b>%s</b>»…\n"
        "Это может занять пару минут." % (esc(name), human_size(real_size), esc(store)))

    stop = asyncio.Event()

    async def ticker() -> None:
        n = 0
        while not stop.is_set():
            try:
                await asyncio.wait_for(stop.wait(), timeout=5)
                return
            except asyncio.TimeoutError:
                n += 12
                try:
                    await status.edit_text(
                        "🔐 Шифрую и заливаю <b>%s</b> (%s) в «<b>%s</b>»… %d сек" %
                        (esc(name), human_size(real_size), esc(store), n))
                except Exception:
                    pass

    tick = asyncio.create_task(ticker())
    try:
        async with lock_for(uid):
            code, out = await run_engine(uid, ["upload", str(dest), store])
    finally:
        stop.set()
        await asyncio.gather(tick, return_exceptions=True)
        with contextlib.suppress(Exception):
            dest.unlink()

    log.info("file: заливка uid=%s завершена кодом %s", uid, code)
    if code == 0:
        m_id = re.search(r"\[id (\S+)\]", out)
        extra = ""
        if m_id:
            extra = "\nID элемента: <code>%s</code>" % esc(m_id.group(1))
        await status.edit_text(
            "✅ <b>Залито в облако</b>\n"
            "• файл: <b>%s</b> (%s)\n• хранилище: <b>%s</b>%s" %
            (esc(name), human_size(real_size), esc(store), extra),
            reply_markup=InlineKeyboardMarkup(inline_keyboard=[[
                InlineKeyboardButton(text="📂 Открыть хранилище", callback_data="s|%s|0" % store)]]),
        )
    else:
        await status.edit_text(
            "❌ <b>Не удалось залить</b> «%s».\n\n<pre>%s</pre>" %
            (esc(name), esc(out[-1200:])))


# -------------------------------------------------- скачивание из облака
@router.callback_query(F.data.startswith("d|"))
async def cb_download(cq: CallbackQuery, bot: Bot) -> None:
    uid = cq.from_user.id
    if not allowed(uid) or not is_registered(uid):
        await cq.answer("Сначала /start", show_alert=True)
        return
    _, store, item_id = cq.data.split("|", 2)
    log.info("dl: uid=%s запросил %s из «%s»", uid, item_id, store)
    if lock_for(uid).locked():
        await cq.answer("Дождитесь окончания текущей операции.", show_alert=True)
        return
    await cq.answer("Готовлю файл…")
    work = user_work_dir(uid)
    out_dir = work / ("dl_%d_%s" % (int(time.time()), item_id))
    out_dir.mkdir(parents=True, exist_ok=True)
    status = await cq.message.answer(
        "⏳ Скачиваю <code>%s</code> из хранилища «%s» и расшифровываю…" % (esc(item_id), esc(store)))
    try:
        async with lock_for(uid):
            code, out = await run_engine(uid, ["download", store, str(out_dir), item_id])
        if code != 0:
            await status.edit_text("❌ <b>Не удалось скачать</b>.\n\n<pre>%s</pre>" % esc(out[-1200:]))
            return

        files = [p for p in out_dir.rglob("*") if p.is_file()]
        if not files:
            await status.edit_text("🤷 Движок не создал файлов — возможно, элемент пуст.")
            return
        if len(files) > 1 or files[0].parent != out_dir:
            zip_path = work / ("item_%s.zip" % item_id)
            with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
                for p in files:
                    z.write(p, p.relative_to(out_dir))
            payload = zip_path
        else:
            payload = files[0]

        size = payload.stat().st_size
        log.info("dl: uid=%s файл %s (%d Б)", uid, payload.name, size)
        if size > TG_SEND_LIMIT:
            await status.edit_text(
                "⚠️ Файл <b>%s</b> — %s. Это больше <b>50 МБ</b>, Bot API не даёт "
                "боту отправить такой файл.\n\nСкачайте его через GUI/CLI: "
                "<code>python GITHUBCLOAD.py download %s &lt;папка&gt; %s</code>" %
                (esc(payload.name), human_size(size), esc(store), esc(item_id)))
            return

        await status.edit_text("📤 Отправляю <b>%s</b> (%s)…" % (esc(payload.name), human_size(size)))
        await bot.send_chat_action(cq.message.chat.id, ChatAction.UPLOAD_DOCUMENT)
        await bot.send_document(
            cq.message.chat.id,
            FSInputFile(payload, filename=payload.name),
            caption="📦 %s\n🔓 расшифровано из хранилища «%s»" % (payload.name, store),
        )
        await status.delete()
    except TelegramForbiddenError:
        pass
    except Exception as exc:
        try:
            await status.edit_text("❌ Ошибка при скачивании: %s" % esc(str(exc)))
        except Exception:
            pass
    finally:
        shutil.rmtree(out_dir, ignore_errors=True)


# ------------------------------------------------------------- удаление
@router.callback_query(F.data.startswith("x|"))
async def cb_delete_ask(cq: CallbackQuery) -> None:
    uid = cq.from_user.id
    if not allowed(uid) or not is_registered(uid):
        await cq.answer("Сначала /start", show_alert=True)
        return
    _, store, item_id = cq.data.split("|", 2)
    await cq.answer()
    await cq.message.edit_text(
        "🗑 Удалить элемент <code>%s</code> из хранилища «%s»?\n"
        "Действие необратимо." % (esc(item_id), esc(store)),
        reply_markup=confirm_kb("del|%s|%s" % (store, item_id)),
    )


@router.callback_query(F.data == "yes|logout")
async def cb_logout_yes(cq: CallbackQuery, state: FSMContext) -> None:
    uid = cq.from_user.id
    forget_user(uid)
    await state.clear()
    await cq.answer("Доступы удалены")
    await cq.message.edit_text("🗑 Готово: доступы удалены. Для новой регистрации — /start.")


@router.callback_query(F.data.startswith("yes|del|"))
async def cb_delete_yes(cq: CallbackQuery) -> None:
    uid = cq.from_user.id
    _, _, store, item_id = cq.data.split("|", 3)
    await cq.answer("Удаляю…")
    async with lock_for(uid):
        code, out = await run_engine(uid, ["delete", store, item_id])
    log.info("del: uid=%s %s/%s → код %s", uid, store, item_id, code)
    if code == 0:
        await cq.message.edit_text("✅ Удалено: <code>%s</code>" % esc(item_id))
        await render_store(cq.message, uid, store, 0, edit=False)
    else:
        await cq.message.edit_text("❌ Не удалось удалить:\n<pre>%s</pre>" % esc(out[-800:]))


@router.callback_query(F.data == "noop")
async def cb_noop(cq: CallbackQuery) -> None:
    await cq.answer()


# ------------------------------------------------- всё остальное
@router.message()
async def on_any(message: Message) -> None:
    """Ответ на то, что не подошло под другие обработчики."""
    if await deny_if_needed(message):
        return
    if message.text:
        await message.answer(
            "🤔 Не понял команду.\n"
            "Отправьте файл — залью в облако, или откройте «📦 Хранилища».\n"
            "Список команд: /help",
            reply_markup=main_menu_kb(),
        )
    else:
        await message.answer(
            "🤔 Такое сообщение не поддерживаю. Пришлите файл документом, фото, видео, "
            "аудио или голосовым — залью его в облако.",
            reply_markup=main_menu_kb(),
        )


# ------------------------------------------------------------- ошибки
@dp.errors()
async def on_error(event) -> bool:
    exc = event.exception
    if isinstance(exc, TelegramRetryAfter):
        await asyncio.sleep(exc.retry_after)
        return True
    log.exception("Ошибка при обработке апдейта: %s", exc)
    try:
        upd = event.update
        msg = upd.message or (upd.callback_query.message if upd.callback_query else None)
        if msg:
            await msg.answer("⚠️ Внутренняя ошибка: %s" % esc(str(exc))[:300])
    except Exception:
        pass
    return True


# ------------------------------------------------------------- запуск
async def set_commands(bot: Bot) -> None:
    await bot.set_my_commands([
        BotCommand(command="start", description="Начало и регистрация"),
        BotCommand(command="storages", description="Хранилища и файлы"),
        BotCommand(command="storage", description="Текущее хранилище / сменить"),
        BotCommand(command="selftest", description="Самопроверка движка"),
        BotCommand(command="status", description="Статус"),
        BotCommand(command="logout", description="Забыть мои доступы"),
        BotCommand(command="cancel", description="Отменить ввод"),
        BotCommand(command="help", description="Помощь"),
    ], scope=BotCommandScopeDefault())
    try:
        await bot.set_my_short_description(
            short_description="Приватное облако на GitHub: шифрование 7z AES-256 и репозитории-тома.")
        await bot.set_my_description(description=(
            "GITHUBCLOAD — приватное облако на GitHub.\n"
            "Отправьте файл — бот зашифрует его (7z, AES-256) и зальёт в ваш приватный "
            "репозиторий-том. Хранилищами можно ходить кнопками, файлы до 50 МБ — скачивать в чат."))
    except Exception:
        pass


def read_token(args) -> str:
    if args.token:
        return args.token.strip()
    env = os.environ.get("GCB_TG_TOKEN", "").strip()
    if env:
        return env
    p = BASE_DIR / "token.txt"
    if p.is_file():
        return p.read_text(encoding="utf-8").strip()
    return ""


async def main() -> int:
    ap = argparse.ArgumentParser(description="GITHUBCLOAD_TG — телеграм-бот приватного облака")
    ap.add_argument("--token", help="токен бота (иначе env GCB_TG_TOKEN или token.txt)")
    ap.add_argument("--version", action="store_true", help="версия и выход")
    args = ap.parse_args()

    if args.version:
        print("GITHUBCLOAD_TG %s" % TG_VERSION)
        return 0

    token = read_token(args)
    if not token:
        print("ОШИБКА: не задан токен бота (--token, GCB_TG_TOKEN или token.txt)")
        return 2
    if not ENGINE.is_file():
        print("ОШИБКА: рядом нет движка %s" % ENGINE_NAME)
        return 2

    DATA_DIR.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        handlers=[logging.FileHandler(LOG_FILE, encoding="utf-8"), logging.StreamHandler()],
    )
    log.info("GITHUBCLOAD_TG %s запускается; данные в %s", TG_VERSION, DATA_DIR)

    session = AiohttpSession(timeout=300)
    bot = Bot(token=token, session=session,
              default=DefaultBotProperties(parse_mode=ParseMode.HTML))
    me = await bot.get_me()
    log.info("Бот: @%s (id=%d)", me.username, me.id)
    await set_commands(bot)
    log.info("Команды бота установлены, начинаю polling")
    try:
        await dp.start_polling(bot, allowed_updates=dp.resolve_used_update_types())
    finally:
        await bot.session.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main()))
    except (KeyboardInterrupt, SystemExit):
        pass
