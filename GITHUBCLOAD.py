#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GITHUBCLOAD — приватное «облачное» хранилище поверх GitHub.

Что делает:
  * шифрует файл/папку паролем из PBEpass.txt (7z, AES-256, библиотека py7zr);
  * если архив больше 8 МБ — режется многотомником на части по 8 МБ (GitHub API
    надёжно принимает части только до ~8 МБ);
  * заливает части в ПРИВАТНЫЙ репозиторий GitHub (PyGithub) по токену из tokengh.txt;
  * объём одного репозитория-тома ограничен (см. MAX_REPO_PAYLOAD), чтобы фактический
    размер репозитория не превышал 1 ГБ; при переполнении автоматически создаётся
    следующий том: имя, имя_2, имя_3, ... ;
  * файлы tokengh.txt и PBEpass.txt НИКОГДА не попадают в репозиторий, даже если
    загружаемая папка их содержит.

Использование:
  python GITHUBCLOAD.py upload <путь> <имя_хранилища>
  python GITHUBCLOAD.py <путь> <имя_хранилища>           (сокращённая форма = upload)
  python GITHUBCLOAD.py -                                список всех хранилищ и файлов
  python GITHUBCLOAD.py list [имя]                       список хранилищ / содержимого
  python GITHUBCLOAD.py repos                            только тома (репозитории)
  python GITHUBCLOAD.py download <имя> <папка_куда> [id] скачивание с расшифровкой
  python GITHUBCLOAD.py delete <имя> <id>                удалить элемент
  python GITHUBCLOAD.py wipe <имя>                       удалить хранилище целиком
  python GITHUBCLOAD.py selftest                         самопроверка без GitHub

Файлы рядом со скриптом:
  tokengh.txt  — токен GitHub (права: repo, при необходимости delete_repo)
  PBEpass.txt  — пароль шифрования (AES-256)
"""

import json
import os
import re
import shutil
import sys
import tempfile
import time
import uuid
from datetime import datetime, timezone

# Подавляем надоедливое предупреждение cryptography о Python 3.8 — это просто
# notice, не ошибка; оно сыпалось в вывод GUI и выглядело как «жалоба на библиотеки».
import warnings
warnings.filterwarnings("ignore", message=r"Python 3\.8 is no longer supported.*")

# --------------------------------------------------------------------------
# Константы
# --------------------------------------------------------------------------
# Папка приложения. Приоритет:
#   1) GCB_APP_DIR — задаёт графический интерфейс, когда ядро распаковано во
#      временную папку (single-file GUI: exe в %TEMP%, а токен/пароль лежат
#      рядом с GITHUBCLOAD_GUI.exe);
#   2) папка самого exe (PyInstaller), __file__ в _MEIxxxx для этого не годится;
#   3) папка скрипта (разработка без сборки).
env_dir = os.environ.get("GCB_APP_DIR")
if env_dir:
    SCRIPT_DIR = os.path.abspath(env_dir)
elif getattr(sys, "frozen", False):
    SCRIPT_DIR = os.path.dirname(os.path.abspath(sys.executable))
else:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TOKEN_FILE = os.path.join(SCRIPT_DIR, "tokengh.txt")
PASS_FILE = os.path.join(SCRIPT_DIR, "PBEpass.txt")

# Части по 8 МБ: GitHub Contents API надёжно принимает файлы до ~8 МБ одним
# запросом (проверено: 21 МБ даёт 401/hang). Поэтому ВСЁ, что больше 8 МБ,
# режется на многотомные части по 8 МБ — работает и для 22 МБ книги, и для гигантов.
PART_SIZE = 8 * 1024 * 1024
SPLIT_IF_SOURCE_BIGGER = 8 * 1024 * 1024
MAX_REPO_PAYLOAD = 900 * 1024 * 1024         # полезная нагрузка одного тома (~900 МБ):
                                             # фактический размер репозитория остаётся <= 1 ГБ
MANIFEST = "_gcb_manifest.json"              # служебный файл в корне тома
DATA_DIR = "gcb"                             # папка в томе с частями архивов
TOOL = "GITHUBCLOAD"
PROTECTED_FILES = {"tokengh.txt", "pbepass.txt"}  # никогда не загружаем (без учёта регистра)

USAGE = r"""
GITHUBCLOAD — приватное «облако» на GitHub (шифрование AES-256 + части по 8 МБ)

ЗАГРУЗКА:
  python GITHUBCLOAD.py upload <путь_к_файлу_или_папке> <имя_хранилища>
  python GITHUBCLOAD.py <путь_к_файлу_или_папке> <имя_хранилища>    (сокращённая форма)

ПРОСМОТР:
  python GITHUBCLOAD.py -           все файлы и имена репозиториев-томов
  python GITHUBCLOAD.py list        то же самое
  python GITHUBCLOAD.py list <имя>  содержимое хранилища (оригинальные имена файлов)
  python GITHUBCLOAD.py repos       список только томов

СКАЧИВАНИЕ (расшифровка «под капотом», имена файлов сохраняются):
  python GITHUBCLOAD.py download <имя> <папка_куда>
  python GITHUBCLOAD.py download <имя> <папка_куда> <id_элемента>

УДАЛЕНИЕ:
  python GITHUBCLOAD.py delete <имя> <id_элемента>
  python GITHUBCLOAD.py wipe <имя>

СЛУЖЕБНОЕ:
  python GITHUBCLOAD.py selftest    проверка шифрования/разбиения (без GitHub)
  python GITHUBCLOAD.py help

Файлы рядом со скриптом: tokengh.txt (токен GitHub), PBEpass.txt (пароль шифрования).
Они никогда не загружаются в репозиторий.
"""


# --------------------------------------------------------------------------
# Мелкие утилиты
# --------------------------------------------------------------------------
def ensure_utf8_console():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def human(n):
    """Человекочитаемый размер."""
    try:
        n = float(n)
    except (TypeError, ValueError):
        return str(n)
    for unit in ("Б", "КБ", "МБ", "ГБ", "ТБ"):
        if n < 1024:
            return ("%d Б" % n) if unit == "Б" else ("%.1f %s" % (n, unit))
        n /= 1024.0
    return "%d Б" % n


def plural(n, one, few, many):
    n = abs(int(n)) % 100
    if 11 <= n <= 19:
        return many
    d = n % 10
    if d == 1:
        return one
    if 2 <= d <= 4:
        return few
    return many


def read_secret(path, label, hint, first_line_only):
    """
    Читает секрет из файла. Строки, начинающиеся с '#' — комментарии-подсказки,
    они игнорируются. first_line_only=True — берём первую непустую строку (токен),
    иначе — все непустые строки, склеенные переводом строки (пароль).
    """
    if not os.path.isfile(path):
        sys.exit("ОШИБКА: не найден файл «%s».\n%s" % (path, hint))
    try:
        with open(path, "r", encoding="utf-8-sig") as fh:
            text = fh.read()
    except OSError as exc:
        sys.exit("ОШИБКА: не удалось прочитать «%s»: %s" % (path, exc))
    lines = [ln.strip() for ln in text.splitlines()
             if ln.strip() and not ln.strip().startswith("#")]
    if first_line_only:
        secret = lines[0] if lines else ""
    else:
        secret = "\n".join(lines)
    if not secret:
        sys.exit("ОШИБКА: файл «%s» пуст (комментарии не считаются).\n%s" % (path, hint))
    return secret


def is_404(exc):
    try:
        from github import GithubException
        return isinstance(exc, GithubException) and exc.status == 404
    except Exception:
        return False


def _imp_fail(name, exc):
    """Понятный текст при сбое импорта: показываем реальную причину (в собранном
    exe это часто «не хватает вложенного модуля» или гонка распаковки PyInstaller)."""
    msg = getattr(exc, "name", "") or str(exc)
    return ("НЕ УДАЛОСЬ ИМПОРТИРОВАТЬ %s (%s).\n"
            "В обычном Python:  python -m pip install py7zr PyGithub\n"
            "В собранном exe: перезапустите приложение — если повторится,\n"
            "пришлите этот текст (особенно строку в скобках)." % (name, msg))


def check_deps():
    try:
        import py7zr  # noqa: F401
    except ImportError as exc:
        sys.exit(_imp_fail("py7zr", exc))


def boot_github():
    """Читает tokengh.txt, подключается. Возвращает (g, user, login)."""
    token = read_secret(
        TOKEN_FILE, "токен GitHub", first_line_only=True,
        hint="Создайте токен на github.com: Settings -> Developer settings -> Personal access tokens\n"
        "Права: repo (полный доступ к приватным репозиториям), delete_repo (нужен для wipe).\n"
        "Вставьте токен в tokengh.txt первой строкой.")
    try:
        import github
    except ImportError as exc:
        sys.exit(_imp_fail("PyGithub (github)", exc))
    try:
        auth = github.Auth.Token(token)
        g = github.Github(auth=auth, timeout=300)
        login = g.get_user().login
    except github.BadCredentialsException:
        sys.exit("ОШИБКА: GitHub не принял токен из tokengh.txt (401 Bad credentials).\n"
                 "Проверьте токен: он должен быть действительным и с правами repo.")
    except Exception as exc:
        sys.exit("ОШИБКА: не удалось подключиться к GitHub: %s" % exc)
    return g, g.get_user(), login


def read_password():
    return read_secret(PASS_FILE, "пароль шифрования", first_line_only=False,
                       hint="Придумайте пароль и впишите его в PBEpass.txt (он же понадобится для расшифровки).")


# --------------------------------------------------------------------------
# Репозитории-тома
# --------------------------------------------------------------------------
def volume_repo_name(base, idx):
    return base if idx == 1 else "%s_%d" % (base, idx)


def guess_base(repo_name):
    """Для нового тома вида 'имя_2' базовое имя — без '_<цифры>'."""
    m = re.match(r"^(.*)_\d+$", repo_name)
    return m.group(1) if m else repo_name


def fetch_gh(repo, gh_path):
    """Читает файл из репозитория как байты ВНЕ зависимости от размера.

    GitHub отдаёт содержимое через Contents API (encoding: base64) только для
    файлов до ~1 МБ; для крупных возвращает encoding: none, и PyGithub падает
    в decoded_content (AssertionError: unsupported encoding: none). Поэтому файл
    всегда берём через Git Blob API (base64, до 100 МБ) — качает и мелкое, и
    многотомные части (в т.ч. по 8 МБ).
    """
    try:
        fc = repo.get_contents(gh_path)
    except Exception as exc:
        raise SystemExit("ОШИБКА: не найден файл %s в репозитории «%s»: %s"
                         % (gh_path, repo.name, exc))
    blob = repo.get_git_blob(fc.sha)
    payload = blob.content if blob.content else ""
    import base64
    return base64.b64decode(payload)


def get_manifest(repo):
    """dict манифеста тома или None, если файла нет / он битый."""
    try:
        content = repo.get_contents(MANIFEST)
        data = json.loads(content.decoded_content.decode("utf-8"))
    except Exception as exc:
        if is_404(exc):
            return None
        if isinstance(exc, (ValueError, TypeError)):
            return None
        raise
    if not isinstance(data, dict):
        return None
    data.setdefault("items", [])
    data.setdefault("payload_bytes", 0)
    data.setdefault("volume_index", 1)
    return data


def write_manifest(repo, manifest, base, idx):
    manifest.update({"tool": TOOL, "format": 1, "base": base, "volume_index": idx})
    blob = json.dumps(manifest, ensure_ascii=False, indent=2).encode("utf-8")
    try:
        content = repo.get_contents(MANIFEST)
        repo.update_file(MANIFEST, "GITHUBCLOAD: обновление манифеста", blob, content.sha)
    except Exception as exc:
        if is_404(exc):
            repo.create_file(MANIFEST, "GITHUBCLOAD: создание манифеста", blob)
        else:
            raise


def repo_root_files(repo):
    try:
        contents = repo.get_contents("")
        if isinstance(contents, list):
            return [c.name for c in contents]
        return [contents.name]
    except Exception as exc:
        if is_404(exc):
            return []
        raise


def create_volume_repo(g, user, base, idx):
    """Создаёт приватный репозиторий-том с манифестом."""
    name = volume_repo_name(base, idx)
    repo = user.create_repo(
        name,
        private=True,
        auto_init=True,
        description="GITHUBCLOAD том %d хранилища «%s»" % (idx, base),
        has_issues=False,
        has_wiki=False,
    )
    try:  # убрать автосозданный README
        readme = repo.get_contents("README.md")
        repo.delete_file("README.md", "GITHUBCLOAD: служебная очистка", readme.sha)
    except Exception:
        pass
    manifest = {"items": [], "payload_bytes": 0, "volume_index": idx}
    write_manifest(repo, manifest, base, idx)
    return repo, manifest


def choose_volume(g, user, login, base, need_bytes):
    """
    Возвращает (repo, manifest) — том, в который поместится элемент.
    Проходит по цепочке base, base_2, ... ; если ни один не подходит — создаёт новый том.
    """
    if need_bytes > MAX_REPO_PAYLOAD:
        sys.exit("ОШИБКА: размер элемента (%.1f МБ) больше лимита одного тома (%.1f МБ).\n"
                 "Разбейте его на части по ~800 МБ и загрузите каждую отдельной командой."
                 % (need_bytes / 1048576.0, MAX_REPO_PAYLOAD / 1048576.0))
    idx = 1
    while True:
        name = volume_repo_name(base, idx)
        repo = None
        try:
            repo = g.get_repo("%s/%s" % (login, name))
        except Exception as exc:
            if not is_404(exc):
                sys.exit("ОШИБКА: не удалось открыть репозиторий «%s»: %s" % (name, exc))
        if repo is None:  # тома нет — создаём
            repo, manifest = create_volume_repo(g, user, base, idx)
            print("Создан приватный репозиторий-том: %s" % name)
            return repo, manifest

        manifest = get_manifest(repo)
        if manifest is None:
            names = repo_root_files(repo)
            if not names or set(names) <= {"README.md"}:
                manifest = {"items": [], "payload_bytes": 0, "volume_index": idx}
                write_manifest(repo, manifest, base, idx)
                print("Репозиторий «%s» принят как пустой том хранилища «%s»." % (name, base))
                return repo, manifest
            sys.exit("ОШИБКА: репозиторий «%s» уже существует и содержит посторонние файлы: %s.\n"
                     "Выберите другое имя хранилища либо удалите/очистите этот репозиторий."
                     % (name, ", ".join(sorted(names)[:8])))
        if manifest.get("tool") != TOOL:
            sys.exit("ОШИБКА: репозиторий «%s» — не том GITHUBCLOAD." % name)
        if manifest.get("base") != base:
            sys.exit("ОШИБКА: репозиторий «%s» относится к хранилищу «%s», а не «%s»."
                     % (name, manifest.get("base"), base))
        payload = int(manifest.get("payload_bytes") or 0)
        if payload + need_bytes <= MAX_REPO_PAYLOAD:
            return repo, manifest
        idx += 1
        if idx > 1000:
            sys.exit("ОШИБКА: слишком много томов (более 1000) у хранилища «%s»." % base)


def iter_chain(g, login, base):
    """(repo, manifest) по томам base, base_2, ... пока цепочка существует."""
    idx = 1
    while idx <= 1000:
        name = volume_repo_name(base, idx)
        try:
            repo = g.get_repo("%s/%s" % (login, name))
        except Exception as exc:
            if is_404(exc):
                return
            sys.exit("ОШИБКА: не удалось открыть репозиторий «%s»: %s" % (name, exc))
        manifest = get_manifest(repo)
        if manifest is None or manifest.get("tool") != TOOL or manifest.get("base") != base:
            if idx == 1:
                sys.exit("ОШИБКА: репозиторий «%s» существует, но это не хранилище GITHUBCLOAD "
                         "(или это том другого хранилища)." % name)
            return  # цепочка закончилась
        yield repo, manifest
        idx += 1


def discover_chains(g, login):
    """
    Сканирует репозитории владельца токена, собирает цепочки томов GITHUBCLOAD.
    Возвращает dict: base -> {idx: (repo_name, manifest)}.
    """
    user = g.get_user()
    chains = {}
    try:
        repos = list(user.get_repos(type="owner"))
    except Exception as exc:
        sys.exit("ОШИБКА: не удалось получить список репозиториев: %s" % exc)
    for repo in repos:
        manifest = get_manifest(repo)
        if manifest is None or manifest.get("tool") != TOOL:
            continue
        base = manifest.get("base") or repo.name
        idx = int(manifest.get("volume_index") or 1)
        chains.setdefault(base, {})[idx] = (repo.name, manifest)
    return chains


# --------------------------------------------------------------------------
# Сбор источника (с защитой tokengh.txt / PBEpass.txt)
# --------------------------------------------------------------------------
def collect_source(src):
    """
    Возвращает (kind, root_name, files):
      kind      — 'file' или 'dir'
      root_name — имя верхнего элемента (файла или папки)
      files     — список (абс_путь, arcname_в_архиве)
    Защищённые файлы пропускаются в любом вложении.
    """
    src = os.path.abspath(src)
    if not os.path.exists(src):
        sys.exit("ОШИБКА: путь не найден: %s" % src)
    base = os.path.basename(os.path.normpath(src))
    if os.path.isfile(src):
        if base.lower() in PROTECTED_FILES:
            sys.exit("ОШИБКА: файл «%s» служебный (tokengh.txt / PBEpass.txt) — загрузка запрещена." % base)
        return "file", base, [(src, base)]

    files, skipped = [], []
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = [d for d in dirnames
                       if d.lower() not in PROTECTED_FILES and d != "__pycache__"]
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            if fn.lower() in PROTECTED_FILES:
                skipped.append(full)
                continue
            rel = os.path.relpath(full, src).replace("\\", "/")
            files.append((full, base + "/" + rel))
    if skipped:
        print("ВНИМАНИЕ: служебные файлы пропущены (никогда не загружаются):")
        for s in skipped:
            print("   - %s" % os.path.relpath(s, src))
    if not files:
        sys.exit("ОШИБКА: в папке «%s» нет файлов для загрузки." % src)
    return "dir", base, files


def source_size(files):
    return sum(os.path.getsize(f) for f, _ in files if os.path.isfile(f))


def make_archive(item_id, src, password, tmpdir,
                 split_trigger=SPLIT_IF_SOURCE_BIGGER, part_size=PART_SIZE, files=None):
    """
    Создаёт зашифрованный 7z-архив источника.
    Если суммарный размер исходных файлов > split_trigger — пишет многотомный архив
    (multivolumefile, тома по part_size). Возвращает список путей частей (отсортирован).
    files можно передать заранее — результат collect_source(src).
    """
    import py7zr

    if files is None:
        _, _, files = collect_source(src)
    total = source_size(files)
    use_volumes = total > split_trigger

    base_path = os.path.join(tmpdir, "%s.7z" % item_id)
    if use_volumes:
        import multivolumefile
        with multivolumefile.MultiVolume(base_path, mode="wb",
                                         volume=part_size, ext_digits=3) as mvf:
            with py7zr.SevenZipFile(mvf, "w", password=password,
                                    header_encryption=True) as sz:
                for full, arcname in files:
                    sz.write(full, arcname=arcname)
        parts = sorted(os.path.join(tmpdir, p) for p in os.listdir(tmpdir)
                       if p.startswith(item_id + ".7z"))
    else:
        with py7zr.SevenZipFile(base_path, "w", password=password,
                                header_encryption=True) as sz:
            for full, arcname in files:
                sz.write(full, arcname=arcname)
        parts = [base_path]
    return parts


# --------------------------------------------------------------------------
# Команды
# --------------------------------------------------------------------------
def cmd_upload(g, user, login, path, base):
    check_deps()
    password = read_password()

    kind, root_name, files = collect_source(path)
    total = source_size(files)
    kind_word = "файл" if kind == "file" else "папка"
    print("Загрузка: %s «%s» (%s) -> хранилище «%s»" % (kind_word, root_name, human(total), base))

    item_id = "%s_%s" % (time.strftime("%Y%m%d_%H%M%S"), uuid.uuid4().hex[:6])
    tmpdir = tempfile.mkdtemp(prefix="gcb_up_")
    try:
        parts = make_archive(item_id, path, password, tmpdir, files=files)
        total_bytes = sum(os.path.getsize(p) for p in parts)
        if len(parts) == 1:
            print("Архив создан: %s (шифрование AES-256)" % human(total_bytes))
        else:
            print("Архив создан и разрезан на %d %s по %s (всего %s)"
                  % (len(parts), plural(len(parts), "часть", "части", "частей"),
                     human(PART_SIZE), human(total_bytes)))

        repo, manifest = choose_volume(g, user, login, base, total_bytes)

        for i, part in enumerate(parts, 1):
            fname = os.path.basename(part)
            with open(part, "rb") as fh:
                data = fh.read()
            gh_path = "%s/%s/%s" % (DATA_DIR, item_id, fname)
            msg = "GITHUBCLOAD: %s [%s], часть %d/%d" % (root_name, item_id, i, len(parts))
            try:
                repo.create_file(gh_path, msg, data)
            except Exception as exc:
                sys.exit("ОШИБКА при загрузке части %d/%d: %s\n"
                         "Если часть уже успела загрузиться, удалите элемент:\n"
                         "  python GITHUBCLOAD.py delete %s %s"
                         % (i, len(parts), exc, base, item_id))
            print("  часть %d/%d -> %s (%s)" % (i, len(parts), repo.name, human(len(data))))

        item = {
            "id": item_id,
            "name": root_name,
            "kind": kind,
            "source_bytes": total,
            "created": now_iso(),
            "bytes": total_bytes,
            "parts": [os.path.basename(p) for p in parts],
        }
        manifest.setdefault("items", []).append(item)
        manifest["payload_bytes"] = int(manifest.get("payload_bytes") or 0) + total_bytes
        idx = int(manifest.get("volume_index") or 1)
        write_manifest(repo, manifest, base, idx)

        print("\nГОТОВО: %s -> том %s" % (root_name, repo.name))
        print("  id элемента: %s" % item_id)
        print("  посмотреть:  python GITHUBCLOAD.py list %s" % base)
        print("  скачать:     python GITHUBCLOAD.py download %s <папка_куда> %s" % (base, item_id))
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def resolve_items(manifest, ref):
    """Элементы по id или по имени."""
    items = manifest.get("items") or []
    if not ref:
        return items
    found = [it for it in items if it.get("id") == ref]
    if found:
        return found
    return [it for it in items if it.get("name") == ref]


def cmd_download(g, user, login, base, dest_dir, item_refs=None):
    """Скачивает элементы хранилища.
    item_refs: None = всё хранилище; список id/имён = только эти элементы."""
    check_deps()
    password = read_password()

    chain = list(iter_chain(g, login, base))
    if not chain:
        sys.exit("ОШИБКА: хранилище «%s» не найдено. Сначала что-нибудь загрузите:\n"
                 "  python GITHUBCLOAD.py upload <путь> %s" % (base, base))
    os.makedirs(dest_dir, exist_ok=True)

    refs = list(item_refs) if item_refs else None
    done = 0
    missing = []
    if refs is None:
        for repo, manifest in chain:
            for it in manifest.get("items") or []:
                _download_item(g, repo, dest_dir, it, password)
                done += 1
    else:
        for ref in refs:
            found = False
            for repo, manifest in chain:
                for it in resolve_items(manifest, ref):
                    _download_item(g, repo, dest_dir, it, password)
                    done += 1
                    found = True
            if not found:
                missing.append(ref)
    if refs is not None and done == 0:
        sys.exit("ОШИБКА: элементы не найдены в хранилище «%s»: %s\n"
                 "Список:  python GITHUBCLOAD.py list %s" % (base, ", ".join(refs), base))
    for m in missing:
        print("ВНИМАНИЕ: элемент не найден и пропущен: %s" % m)
    print("\nГОТОВО: скачано и расшифровано элементов: %d -> %s" % (done, os.path.abspath(dest_dir)))


def _download_item(g, repo, dest_dir, item, password):
    import py7zr

    item_id, name = item["id"], item["name"]
    tmpdir = tempfile.mkdtemp(prefix="gcb_dl_")
    try:
        local_parts = []
        for fname in sorted(item.get("parts") or []):
            gh_path = "%s/%s/%s" % (DATA_DIR, item_id, fname)
            data = fetch_gh(repo, gh_path)
            lp = os.path.join(tmpdir, fname)
            with open(lp, "wb") as fh:
                fh.write(data)
            local_parts.append(lp)
        if not local_parts:
            print("  ВНИМАНИЕ: у элемента %s нет файлов — пропуск." % item_id)
            return
        if len(local_parts) == 1:
            joined = local_parts[0]
        else:  # склеиваем части в единый архив
            joined = os.path.join(tmpdir, "%s_full.7z" % item_id)
            with open(joined, "wb") as out:
                for p in local_parts:
                    with open(p, "rb") as fh:
                        shutil.copyfileobj(fh, out, 1024 * 1024)
        with py7zr.SevenZipFile(joined, "r", password=password) as sz:
            sz.extractall(path=dest_dir)
        target = os.path.join(dest_dir, name)
        print("  %s «%s» -> %s" % (item_id, name, target))
    except Exception as exc:
        sys.exit("ОШИБКА при скачивании/расшифровке «%s» (%s): %s\n"
                 "Проверьте, что в PBEpass.txt тот же пароль, что был при загрузке."
                 % (name, item_id, exc))
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def cmd_delete(g, user, login, base, item_ref):
    chain = list(iter_chain(g, login, base))
    if not chain:
        sys.exit("ОШИБКА: хранилище «%s» не найдено." % base)
    removed = 0
    for repo, manifest in chain:
        for it in resolve_items(manifest, item_ref):
            item_id = it["id"]
            for fname in it.get("parts") or []:
                gh_path = "%s/%s/%s" % (DATA_DIR, item_id, fname)
                try:
                    c = repo.get_contents(gh_path)
                    repo.delete_file(gh_path, "GITHUBCLOAD: удаление элемента %s" % item_id, c.sha)
                except Exception as exc:
                    if not is_404(exc):
                        print("  ВНИМАНИЕ: не удалено %s: %s" % (gh_path, exc))
            manifest["items"] = [x for x in manifest.get("items") or [] if x.get("id") != item_id]
            manifest["payload_bytes"] = max(0, int(manifest.get("payload_bytes") or 0)
                                            - int(it.get("bytes") or 0))
            idx = int(manifest.get("volume_index") or 1)
            write_manifest(repo, manifest, base, idx)
            print("Удалён элемент %s («%s») из тома %s" % (item_id, it.get("name"), repo.name))
            removed += 1
    if removed == 0:
        sys.exit("ОШИБКА: элемент «%s» не найден в хранилище «%s»." % (item_ref, base))


def cmd_wipe(g, user, login, base):
    chain = list(iter_chain(g, login, base))
    if not chain:
        sys.exit("ОШИБКА: хранилище «%s» не найдено." % base)
    names = ", ".join(r.name for r, _ in chain)
    answer = input("БЕЗВОЗВРАТНО удалить репозитории-тома хранилища «%s»: %s?\n"
                   "Введите yes для подтверждения: " % (base, names))
    if answer.strip().lower() != "yes":
        print("Отменено.")
        return
    for repo, _ in chain:
        try:
            repo.delete()
            print("Удалён репозиторий-том: %s" % repo.name)
        except Exception as exc:
            print("ВНИМАНИЕ: не удалось удалить %s: %s" % (repo.name, exc))


# --------------------------------------------------------------------------
# Просмотр
# --------------------------------------------------------------------------
def show_manifest_item(it, indent="     "):
    name = it.get("name", "?")
    kind = "папка" if it.get("kind") == "dir" else "файл"
    print("%s%s" % (indent, name))
    print("%sid: %s | %s | исходный: %s | в облаке: %s | %d %s | %s"
          % (indent + "    ", it.get("id"), kind, human(it.get("source_bytes")),
             human(it.get("bytes")), len(it.get("parts") or []),
             plural(len(it.get("parts") or []), "часть", "части", "частей"),
             it.get("created", "?")))


def cmd_list(g, user, login, base=None):
    if base:
        chain = list(iter_chain(g, login, base))
        if not chain:
            print("Хранилище «%s» не найдено." % base)
            return
        print("Хранилище GITHUBCLOAD «%s»:" % base)
        for repo, manifest in chain:
            _print_volume(repo.name, manifest)
    else:
        chains = discover_chains(g, login)
        if not chains:
            print("Хранилища GITHUBCLOAD не найдены.\nЗагрузите первый файл:\n"
                  "  python GITHUBCLOAD.py upload <путь> <имя>")
            return
        print("Найдено хранилищ GITHUBCLOAD: %d\n" % len(chains))
        for base_name in sorted(chains):
            print("ХРАНИЛИЩЕ «%s» (%d %s):"
                  % (base_name, len(chains[base_name]),
                     plural(len(chains[base_name]), "том", "тома", "томов")))
            for idx in sorted(chains[base_name]):
                repo_name, manifest = chains[base_name][idx]
                _print_volume(repo_name, manifest, indent="  ")
            print()


def _print_volume(repo_name, manifest, indent=""):
    print("%sТОМ %s  (занято %s из ~%s)"
          % (indent, repo_name, human(manifest.get("payload_bytes")), human(MAX_REPO_PAYLOAD)))
    items = manifest.get("items") or []
    if not items:
        print("%s   (пусто)" % indent)
    for it in items:
        print("%s- %s  [id %s]  %s" % (indent, it.get("name"), it.get("id"),
                                       human(it.get("bytes"))))


def cmd_repos(g, user, login):
    chains = discover_chains(g, login)
    if not chains:
        print("Репозитории-тома GITHUBCLOAD не найдены.")
        return
    for base_name in sorted(chains):
        names = ", ".join(chains[base_name][idx][0] for idx in sorted(chains[base_name]))
        print("%s  ->  %s" % (base_name, names))


# --------------------------------------------------------------------------
# Самопроверка (без GitHub)
# --------------------------------------------------------------------------
def selftest():
    check_deps()
    print("Самопроверка GITHUBCLOAD (шифрование AES-256 + многотомность; GitHub не нужен)...")
    import random
    tmp = tempfile.mkdtemp(prefix="gcb_selftest_")
    try:
        src_dir = os.path.join(tmp, "source")
        os.makedirs(src_dir)
        # защищённые имена не должны попасть в архив
        with open(os.path.join(src_dir, "tokengh.txt"), "w") as fh:
            fh.write("секрет1")
        with open(os.path.join(src_dir, "PBEpass.txt"), "w") as fh:
            fh.write("секрет2")
        with open(os.path.join(src_dir, "hello.txt"), "w", encoding="utf-8") as fh:
            fh.write("Привет, GITHUBCLOAD! " * 100)
        os.makedirs(os.path.join(src_dir, "sub"))
        with open(os.path.join(src_dir, "sub", "data.bin"), "wb") as fh:
            fh.write(bytes(random.getrandbits(8) for _ in range(6 * 1024 * 1024)))

        parts = make_archive("selftest", src_dir, "пароль-тест", tmp,
                             split_trigger=1024 * 1024, part_size=1024 * 1024)
        assert len(parts) > 1, "многотомность не сработала"
        print("Создано %d частей: %s"
              % (len(parts), ", ".join(human(os.path.getsize(p)) for p in parts)))

        joined = os.path.join(tmp, "joined.7z")
        with open(joined, "wb") as out:
            for p in sorted(parts):
                with open(p, "rb") as fh:
                    shutil.copyfileobj(fh, out, 1024 * 1024)

        import py7zr
        out_dir = os.path.join(tmp, "out")
        with py7zr.SevenZipFile(joined, "r", password="пароль-тест") as sz:
            sz.extractall(path=out_dir)

        restored = set()
        for dp, _, fns in os.walk(out_dir):
            for fn in fns:
                restored.add(fn.lower())
        assert "hello.txt" in restored and "data.bin" in restored, \
            "файлы не восстановились: %r" % sorted(restored)
        assert not (restored & {"tokengh.txt", "pbepass.txt"}), "защищённые файлы попали в архив!"
        with open(os.path.join(out_dir, "source", "hello.txt"), encoding="utf-8") as fh:
            assert fh.read().startswith("Привет, GITHUBCLOAD!"), "содержимое не совпадает"
        print("Расшифровка и восстановление структуры: OK")
        print("tokengh.txt / PBEpass.txt в архив не попали: OK")
        print("\nСАМОПРОВЕРКА ПРОЙДЕНА УСПЕШНО")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def main(argv):
    ensure_utf8_console()
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(USAGE)
        return 0

    cmd = argv[0].lower()
    try:
        if cmd == "-" or cmd == "list":
            g, user, login = boot_github()
            cmd_list(g, user, login, argv[1] if len(argv) > 1 else None)
        elif cmd == "repos":
            g, user, login = boot_github()
            cmd_repos(g, user, login)
        elif cmd == "upload":
            if len(argv) < 3:
                sys.exit("Использование: python GITHUBCLOAD.py upload <путь> <имя_хранилища>")
            g, user, login = boot_github()
            cmd_upload(g, user, login, argv[1], argv[2])
        elif cmd == "download":
            if len(argv) < 3:
                sys.exit("Использование: python GITHUBCLOAD.py download <имя> <папка_куда> [id ...]")
            g, user, login = boot_github()
            cmd_download(g, user, login, argv[1], argv[2],
                         argv[3:] if len(argv) > 3 else None)
        elif cmd == "delete":
            if len(argv) < 3:
                sys.exit("Использование: python GITHUBCLOAD.py delete <имя> <id_элемента>")
            g, user, login = boot_github()
            cmd_delete(g, user, login, argv[1], argv[2])
        elif cmd == "wipe":
            if len(argv) < 2:
                sys.exit("Использование: python GITHUBCLOAD.py wipe <имя>")
            g, user, login = boot_github()
            cmd_wipe(g, user, login, argv[1])
        elif cmd == "selftest":
            selftest()
        else:
            # сокращённая форма загрузки: python GITHUBCLOAD.py <путь> <имя>
            if len(argv) < 2:
                sys.exit("Использование: python GITHUBCLOAD.py <путь_к_файлу_или_папке> <имя_хранилища>")
            g, user, login = boot_github()
            cmd_upload(g, user, login, argv[0], argv[1])
    except SystemExit:
        raise
    except KeyboardInterrupt:
        print("\nПрервано пользователем.")
        return 130
    except Exception as exc:
        print("ОШИБКА: %s" % exc)
        if os.environ.get("GITHUBCLOAD_DEBUG"):
            import traceback
            traceback.print_exc()
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
