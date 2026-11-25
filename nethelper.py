#!/usr/bin/env python3
"""
nethelper.py - сетевой мост для TINY BROWSER.

QEMU подключает COM1 к UNIX-сокету. Ядро шлёт "GET <url>\n", помощник
реально загружает страницу через urllib и отвечает:

    u32 (little-endian) длина + тело страницы (latin-1, 1 байт = 1 символ)

Так ядро получает настоящий интернет без собственного TCP/IP-стека.
"""
import html
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

SOCK = "/tmp/tinyos-net.sock"
MAX_BODY = 60000
TIMEOUT = 15
BASE = os.path.dirname(os.path.abspath(__file__))
PKG_DB = os.path.join(BASE, "installed_packages.json")
APT_DB = os.path.join(BASE, "apt_installed.json")
PKG_CACHE = os.path.join(BASE, "pkg_cache")
FS_ROOT = os.path.join(BASE, "tinyos_fs")
SITE_DIR = os.path.join(FS_ROOT, "site-packages")


def rss_to_html(body: str) -> str:
    """Make Bing's RSS compact and clickable for the tiny HTML renderer."""
    def link_repl(m: "re.Match") -> str:
        u = html.unescape(m.group(1)).strip()
        if not u.startswith(("http://", "https://")):
            return ""
        return '<a href="%s">%s</a>' % (u, u)

    body = re.sub(r"<link>(.*?)</link>", link_repl, body, flags=re.S)
    body = re.sub(r"<copyright>.*?</copyright>", "", body, flags=re.S)
    body = body.replace("<item>", "<hr><b>").replace("</item>", "</b>")
    body = body.replace('<rss version="2.0">', "").replace("</rss>", "")
    body = body.replace("<channel>", "").replace("</channel>", "")
    return body


def clean_html(body: str) -> str:
    """Drop scripts, styles and comments so more of the page fits in 60 KB."""
    body = re.sub(r"(?is)<script\b[^>]*>.*?</script\s*>", "", body)
    body = re.sub(r"(?is)<style\b[^>]*>.*?</style\s*>", "", body)
    body = re.sub(r"(?is)<noscript\b[^>]*>.*?</noscript\s*>", "", body)
    body = re.sub(r"(?is)<!--.*?-->", "", body)
    body = re.sub(r"[ \t]{2,}", " ", body)
    body = re.sub(r"(?:\r?\n){3,}", "\n\n", body)
    return body


def fetch(url: str) -> str:
    url = url.strip()
    if not url:
        raise ValueError("пустой адрес")

    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in ("http", "https"):
        url = "http://" + url

    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "TinyOS/0.1 (+tiny browser)",
            "Accept": "text/html,text/plain,*/*",
        },
    )
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        data = resp.read(MAX_BODY)

    # latin-1 сохраняет каждый байт как один символ - шрифт ядра 8-битный.
    text = data.decode("latin-1", errors="replace")
    if "format=rss" in url:
        text = rss_to_html(text)
    else:
        text = clean_html(text)
    return text


def error_page(url: str, exc: Exception) -> str:
    def esc(s: str) -> str:
        return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))

    return (
        "<h1>NETWORK ERROR</h1>"
        "<p>Could not load: " + esc(url) + "</p>"
        "<p>" + esc(str(exc)) + "</p>"
        "<p>Check the address and the host connection.</p>"
        "<p><a href=\"http://example.com\">Back to example.com</a></p>"
    )


def pkg_load() -> dict:
    try:
        with open(PKG_DB, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def pkg_save(db: dict) -> None:
    with open(PKG_DB, "w", encoding="utf-8") as fh:
        json.dump(db, fh, indent=2, sort_keys=True, ensure_ascii=False)


def pypi_info(name: str) -> tuple:
    url = "https://pypi.org/pypi/%s/json" % urllib.parse.quote(name, safe="")
    req = urllib.request.Request(
        url, headers={"User-Agent": "TinyOS/0.2 (tiny shell package manager)"}
    )
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        data = json.load(resp)
    info = data.get("info", {})
    return (
        str(info.get("name", name)),
        str(info.get("version", "?")),
        str(info.get("summary", "") or "").replace("\n", " ")[:62],
    )


def pkg_command(line: str) -> str:
    """pip/pkg-like commands from the TINY SHELL; package data lives on the host."""
    parts = line.split()
    cmd = parts[0] if parts else ""
    if cmd == "PKGLIST":
        db = pkg_load()
        if not db:
            return "No packages installed.\nUse: pip install <name>"
        return "\n".join(
            "%s %s\n  %s" % (name, data.get("version", "?"), data.get("summary", ""))
            for name, data in sorted(db.items())
        )

    if len(parts) < 2 or not re.fullmatch(r"[A-Za-z0-9_.-]{1,60}", parts[1]):
        return "Invalid package name. Use letters, digits, . _ -"

    name = parts[1]
    db = pkg_load()
    if cmd == "PKGINSTALL":
        canonical, version, summary = pypi_info(name)
        db[canonical] = {"version": version, "summary": summary}
        pkg_save(db)
        return "Installed %s %s\n  %s" % (canonical, version, summary)

    if cmd == "PKGUNINSTALL":
        key = next((k for k in db if k.lower() == name.lower()), None)
        if key is None:
            return "Package '%s' is not installed." % name
        del db[key]
        pkg_save(db)
        return "Removed %s" % key

    return "Unknown package command: %s" % cmd



# ------------------------- system packages (RPM) --------------------------
# apt/pkg is backed by Fedora's dnf: packages are really downloaded,
# unpacked under tinyos_fs and executed in a bubblewrap sandbox.

def apt_load() -> dict:
    try:
        with open(APT_DB, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def apt_save(db: dict) -> None:
    with open(APT_DB, "w", encoding="utf-8") as fh:
        json.dump(db, fh, indent=2, sort_keys=True, ensure_ascii=False)


def host_env() -> dict:
    env = dict(os.environ)
    env.update({"LANG": "C", "LC_ALL": "C", "GIT_TERMINAL_PROMPT": "0"})
    return env


def run_host(argv: list, timeout: int = 180, cwd: str = None) -> tuple:
    """Run a host command, return (returncode, combined output)."""
    try:
        done = subprocess.run(argv, capture_output=True, text=True,
                              cwd=cwd or BASE, timeout=timeout,
                              env=host_env(), errors="replace")
    except subprocess.TimeoutExpired:
        return 124, "Command timed out after %d seconds." % timeout
    except FileNotFoundError:
        return 127, "Command not found on host: %s" % argv[0]
    text = (done.stdout or "") + (done.stderr or "")
    return done.returncode, text.replace("\r", "")


def wrap_text(text: str, width: int = 78, limit: int = 4000) -> str:
    lines = []
    for line in text.splitlines():
        line = line.rstrip()
        while len(line) > width:
            lines.append(line[:width])
            line = line[width:]
        lines.append(line)
    out = "\n".join(lines).strip()
    return out[:limit] if out else "(no output)"


def rpm_meta(path: str) -> tuple:
    rc, out = run_host(["rpm", "-qp", "--qf",
                        "%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\t%{SIZE}",
                        path], 30)
    if rc != 0:
        return None
    parts = out.strip().split("\t")
    if len(parts) < 4:
        return None
    try:
        size = int(parts[3])
    except ValueError:
        size = 0
    return parts[0], parts[1], parts[2], size


def rpm_files(path: str) -> list:
    rc, out = run_host(["rpm", "-qpl", path], 60)
    if rc != 0:
        return []
    files = []
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("/") and ".." not in line.split("/"):
            files.append(line)
    return files


def extract_rpm(path: str) -> tuple:
    """Unpack an RPM into FS_ROOT without dpkg/rpm database."""
    try:
        p1 = subprocess.Popen(["rpm2cpio", path], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
        p2 = subprocess.Popen(["cpio", "-idm", "--quiet",
                               "--no-absolute-filenames"],
                              stdin=p1.stdout, cwd=FS_ROOT,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        p1.stdout.close()
        out, err = p2.communicate(timeout=180)
        p1.wait(timeout=10)
    except FileNotFoundError as exc:
        return 127, "Missing host tool: %s" % exc
    except subprocess.TimeoutExpired:
        return 124, "Unpacking timed out."
    text = (err or out or b"").decode("utf-8", "replace").replace("\r", "")
    return p2.returncode, text


def fs_unlink(path: str) -> bool:
    full = os.path.normpath(os.path.join(FS_ROOT, path.lstrip("/")))
    if full != FS_ROOT and not full.startswith(FS_ROOT + os.sep):
        return False
    try:
        if os.path.islink(full) or os.path.isfile(full):
            os.unlink(full)
            return True
    except OSError:
        pass
    return False


def remove_pkg_files(name: str, db: dict) -> int:
    entry = db.get(name, {})
    owned = set()
    for other, data in db.items():
        if other != name:
            owned.update(data.get("files", []))
    removed = 0
    dirs = set()
    for path in entry.get("files", []):
        if path in owned:
            continue
        if fs_unlink(path):
            removed += 1
        parent = os.path.dirname(path)
        while parent and parent != "/":
            dirs.add(parent)
            parent = os.path.dirname(parent)
    for d in sorted(dirs, key=lambda p: p.count("/"), reverse=True):
        full = os.path.normpath(os.path.join(FS_ROOT, d.lstrip("/")))
        if full != FS_ROOT and full.startswith(FS_ROOT + os.sep):
            try:
                os.rmdir(full)
            except OSError:
                pass
    return removed


def apt_update() -> str:
    rc, out = run_host(["dnf", "-q", "-y", "makecache"], 240)
    if rc == 127:
        return out
    if rc != 0 and "error" in out.lower():
        return "apt update failed:\n" + wrap_text(out, 78, 1200)
    return "Repositories updated.\nUse: apt search TERM | apt install NAME"


def apt_search(term: str) -> str:
    if not term:
        return "usage: apt search TERM"
    rc, out = run_host(["dnf", "-q", "search", term], 120)
    if rc == 127:
        return out
    lines = [l.rstrip() for l in out.splitlines()
             if l.strip() and not l.startswith("Last metadata")]
    if not lines:
        return "No packages found for '%s'." % term
    return wrap_text("\n".join(lines[:30]), 78, 3000)


def apt_info(name: str) -> str:
    if not name:
        return "usage: apt show NAME"
    rc, out = run_host(["dnf", "-q", "info", name], 120)
    if rc == 127:
        return out
    if rc != 0 and not out.strip():
        return "Package '%s' not found." % name
    return wrap_text(out, 78, 2500)


def apt_list(show_all: bool = False) -> str:
    db = apt_load()
    if not db:
        return "No system packages installed.\nUse: apt update && apt install NAME"
    lines = ["NAME              VERSION            SIZE"]
    for name, data in sorted(db.items()):
        mark = "" if data.get("auto") else "*"
        lines.append("%-17s %-18s %8dK %s" % (
            (name + mark)[:17], str(data.get("version", "?"))[:18],
            int(data.get("size", 0)) // 1024, data.get("arch", "")))
    lines.append("(* = installed explicitly)")
    return "\n".join(lines)


def apt_files(name: str) -> str:
    db = apt_load()
    key = next((k for k in db if k.lower() == name.lower()), None)
    if key is None:
        return "Package '%s' is not installed." % name
    files = db[key].get("files", [])
    if not files:
        return "No files recorded for %s." % key
    return "\n".join(files[:80])


def apt_clean() -> str:
    shutil.rmtree(PKG_CACHE, ignore_errors=True)
    return "Package cache cleaned."


def apt_install(specs: list, force: bool = False) -> str:
    if not specs:
        return "usage: apt install NAME [NAME...]"
    for spec in specs:
        if not re.fullmatch(r"[A-Za-z0-9_.+:-]{1,80}", spec):
            return "Invalid package name: %s" % spec
    db = apt_load()
    todo = []
    for spec in specs:
        key = next((k for k in db if k.lower() == spec.lower()), None)
        if key is not None and not force:
            continue
        todo.append(spec)
    if not todo:
        return "Already installed: " + ", ".join(specs)

    os.makedirs(PKG_CACHE, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="dl-", dir=PKG_CACHE)
    try:
        rc, out = run_host(["dnf", "-q", "-y", "download", "--resolve",
                            "--destdir", tmp] + todo, 300)
        rpms = sorted(
            os.path.join(tmp, f) for f in os.listdir(tmp) if f.endswith(".rpm")
        )
        if not rpms:
            msg = "Package not found or download failed:\n" + wrap_text(out, 78, 1200)
            return msg
        installed = []
        failed = []
        for rpm in rpms:
            meta = rpm_meta(rpm)
            if meta is None:
                failed.append(os.path.basename(rpm))
                continue
            name, version, arch, size = meta
            files = rpm_files(rpm)
            if force and name in db:
                remove_pkg_files(name, db)
            rc2, err = extract_rpm(rpm)
            if rc2 != 0:
                failed.append("%s: %s" % (name, wrap_text(err, 78, 300)))
                continue
            old = db.get(name, {})
            db[name] = {
                "version": version,
                "arch": arch,
                "size": size,
                "files": files,
                "auto": False if name in specs else old.get("auto", True),
            }
            installed.append("%s %s" % (name, version))
        apt_save(db)
        lines = []
        if installed:
            lines.append("Installed: " + ", ".join(installed))
            total = sum(int(db[n].get("size", 0)) for n in db
                        if n in [i.split(" ")[0] for i in installed])
            lines.append("Files: %d, size: %d KiB" % (
                sum(len(db[i.split(" ")[0]].get("files", [])) for i in installed),
                total // 1024))
            lines.append("Run an installed program by typing its name.")
        if failed:
            lines.append("Failed: " + "; ".join(failed[:3]))
        return wrap_text("\n".join(lines) if lines else out, 78, 3000)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def apt_remove(names: list) -> str:
    if not names:
        return "usage: apt remove NAME [NAME...]"
    db = apt_load()
    removed, missing = [], []
    for name in names:
        key = next((k for k in db if k.lower() == name.lower()), None)
        if key is None:
            missing.append(name)
            continue
        count = remove_pkg_files(key, db)
        del db[key]
        removed.append("%s (%d files)" % (key, count))
    apt_save(db)
    lines = []
    if removed:
        lines.append("Removed: " + ", ".join(removed))
    if missing:
        lines.append("Not installed: " + ", ".join(missing))
    return "\n".join(lines)


def apt_upgrade(names: list) -> str:
    db = apt_load()
    if not db:
        return "No system packages installed."
    if names:
        return apt_install(names, force=True)
    rc, out = run_host(["dnf", "-q", "check-update"], 240)
    candidates = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and "." in parts[0]:
            name = parts[0].rsplit(".", 1)[0]
            if name in db:
                candidates.append(name)
    if not candidates:
        return "All installed packages are up to date."
    return apt_install(sorted(set(candidates)), force=True)


def installed_path(cmd: str):
    for d in ("usr/bin", "usr/sbin", "bin", "sbin"):
        full = os.path.join(FS_ROOT, d, cmd)
        if os.path.isfile(full) and os.access(full, os.X_OK):
            return "/" + d + "/" + cmd, full
    return None, None


def apt_which(cmd: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_.+-]{1,60}", cmd or ""):
        return "Invalid command name."
    path, _ = installed_path(cmd)
    return path if path else "%s: not found (try: apt install %s)" % (cmd, cmd)


def strip_ansi(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)


def run_installed(argv: list, cwd_rel: str = "/") -> str:
    if not argv:
        return "usage: <installed-command> [args]"
    cmd = argv[0]
    if not re.fullmatch(r"[A-Za-z0-9_.+-]{1,60}", cmd):
        return "Invalid command name."
    sandbox_path, host_path = installed_path(cmd)
    if sandbox_path is None:
        return "%s: command not found (try: apt install %s)" % (cmd, cmd)

    try:
        cwd = fs_path(cwd_rel)
    except ValueError:
        cwd = FS_ROOT
    if not os.path.isdir(cwd):
        cwd = FS_ROOT
    args = ["bwrap", "--ro-bind", "/", "/", "--dev", "/dev",
            "--proc", "/proc", "--tmpfs", "/tmp",
            "--overlay-src", "/usr",
            "--overlay-src", os.path.join(FS_ROOT, "usr"),
            "--ro-overlay", "/usr"]
    if os.path.isdir(os.path.join(FS_ROOT, "etc")):
        args += ["--overlay-src", "/etc",
                 "--overlay-src", os.path.join(FS_ROOT, "etc"),
                 "--ro-overlay", "/etc"]
    args += ["--unshare-net", "--unshare-pid", "--die-with-parent",
             "--chdir", cwd,
             "--setenv", "PATH", "/usr/local/bin:/usr/bin:/bin",
             "--setenv", "HOME", "/tmp", "--setenv", "TERM", "dumb",
             "--setenv", "LANG", "C",
             "--", sandbox_path] + argv[1:]
    try:
        done = subprocess.run(args, capture_output=True, text=True,
                              timeout=20, errors="replace")
    except subprocess.TimeoutExpired:
        return "Command timed out (interactive/TUI programs are not supported)."
    except FileNotFoundError:
        # No bwrap: run the extracted binary directly.
        try:
            done = subprocess.run([host_path] + argv[1:], capture_output=True,
                                  text=True, timeout=20, cwd=cwd,
                                  errors="replace")
        except subprocess.TimeoutExpired:
            return "Command timed out (interactive/TUI programs are not supported)."
        except OSError as exc:
            return "Cannot run %s: %s" % (cmd, exc)
    text = strip_ansi((done.stdout or "") + (done.stderr or ""))
    text = wrap_text(text, 78, 4000)
    if done.returncode != 0:
        text += "\n(exit code %d)" % done.returncode
    return text


# --------------------------- virtual file system --------------------------
# The shell in TINY OS sees FS_ROOT as "/". Everything stays under BASE.

def fs_path(rel: str) -> str:
    rel = (rel or "").strip().strip("/")
    if ".." in rel.split("/") or "\\" in rel or "\x00" in rel:
        raise ValueError("invalid path")
    path = os.path.normpath(os.path.join(FS_ROOT, rel)) if rel else FS_ROOT
    if path != FS_ROOT and not path.startswith(FS_ROOT + os.sep):
        raise ValueError("path outside the tiny filesystem")
    return path


def fs_list(rel: str) -> str:
    path = fs_path(rel)
    if not os.path.isdir(path):
        return "Not a directory: %s" % (rel or "/")
    lines = []
    for name in sorted(os.listdir(path))[:200]:
        full = os.path.join(path, name)
        if os.path.isdir(full):
            lines.append("d  %s/" % name)
        else:
            lines.append("f  %s  %d bytes" % (name, os.path.getsize(full)))
    return "\n".join(lines) if lines else "(empty directory)"


def fs_cat(rel: str) -> str:
    path = fs_path(rel)
    if not os.path.isfile(path):
        return "Not a file: %s" % rel
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read(4000).replace("\r", "")


def fs_mkdir(rel: str) -> str:
    path = fs_path(rel)
    if not rel or not re.fullmatch(r"[A-Za-z0-9_./-]{1,120}", rel):
        return "Invalid directory name."
    if os.path.exists(path):
        return "Already exists: %s" % rel
    os.makedirs(path)
    return "Created /%s" % rel


def run_cmd(argv: list, timeout: int = 60) -> str:
    env = dict(os.environ)
    env.update({"GIT_TERMINAL_PROMPT": "0", "GIT_CONFIG_NOSYSTEM": "1",
                "GIT_CONFIG_GLOBAL": os.devnull})
    try:
        done = subprocess.run(argv, capture_output=True, text=True,
                              cwd=FS_ROOT, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return "Command timed out after %d seconds." % timeout
    except FileNotFoundError:
        return "Command not found on host: %s" % argv[0]
    text = (done.stdout or done.stderr or "").strip().replace("\r", "")
    if done.returncode != 0 and not text:
        return "Failed (exit %d)." % done.returncode
    return text[:4000] if text else "Done."




def fs_write(rel: str, encoded: str) -> str:
    path = fs_path(rel)
    if not os.path.basename(path):
        return "Invalid file path."
    data = encoded.replace("\\n", "\n")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(data)
    return "Wrote %d bytes to %s" % (len(data.encode("utf-8")), rel or "/")


def fs_touch(rel: str) -> str:
    path = fs_path(rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a", encoding="utf-8"):
        pass
    return "Created %s" % rel


def fs_remove(rel: str) -> str:
    path = fs_path(rel)
    if path == FS_ROOT:
        return "Cannot remove the root directory."
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)
    elif os.path.lexists(path):
        os.unlink(path)
    else:
        return "Not found: %s" % rel
    return "Removed %s" % rel


def fs_move(src: str, dst: str) -> str:
    a, b = fs_path(src), fs_path(dst)
    if a == b or a == FS_ROOT:
        return "Invalid move."
    os.makedirs(os.path.dirname(b), exist_ok=True)
    shutil.move(a, b)
    return "Moved %s -> %s" % (src, dst)


def fs_copy(src: str, dst: str) -> str:
    a, b = fs_path(src), fs_path(dst)
    if os.path.isdir(a):
        shutil.copytree(a, b, dirs_exist_ok=True)
    else:
        os.makedirs(os.path.dirname(b), exist_ok=True)
        shutil.copy2(a, b)
    return "Copied %s -> %s" % (src, dst)


def fs_walk(rel: str):
    root = fs_path(rel)
    files, dirs, total = 0, 0, 0
    for base, dnames, fnames in os.walk(root):
        dirs += len(dnames)
        for name in fnames:
            try:
                total += os.path.getsize(os.path.join(base, name))
                files += 1
            except OSError:
                pass
    return files, dirs, total


def fs_du(rel: str) -> str:
    files, dirs, total = fs_walk(rel)
    return ("%s: %d files, %d directories, %d bytes (%.1f KiB)"
            % (rel or "/", files, dirs, total, total / 1024.0))


def fs_tree(rel: str) -> str:
    root = fs_path(rel)
    lines = []
    for base, dnames, fnames in os.walk(root):
        depth = os.path.relpath(base, root).count(os.sep)
        if depth >= 4:
            dnames[:] = []
            continue
        indent = "  " * depth
        for name in sorted(dnames)[:40]:
            lines.append(indent + name + "/")
        for name in sorted(fnames)[:40]:
            lines.append(indent + name)
        if len(lines) > 150:
            break
    return "\n".join(lines[:150]) if lines else "(empty)"


def fs_lines(rel: str, tail: bool = False) -> str:
    path = fs_path(rel)
    if not os.path.isfile(path):
        return "Not a file: %s" % rel
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read(20000).splitlines()
    chosen = lines[-12:] if tail else lines[:12]
    return "\n".join(chosen) if chosen else "(empty)"


def fs_wc(rel: str) -> str:
    path = fs_path(rel)
    if not os.path.isfile(path):
        return "Not a file: %s" % rel
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read(200000)
    return "%s: %d lines, %d words, %d bytes" % (
        rel, len(text.splitlines()), len(text.split()), len(text.encode("utf-8")))


def git_repo_path(rel: str):
    """Return the host path only if it is itself a git repository root."""
    try:
        path = fs_path(rel or ".")
    except ValueError:
        return None
    rc, out = run_host(["git", "-C", path, "rev-parse", "--show-toplevel"], 10)
    if rc != 0:
        return None
    if os.path.normpath(out.strip()) != os.path.normpath(path):
        return None
    return path


def git_clone(url: str) -> str:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in ("http", "https") or not parsed.netloc:
        return "git clone supports http:// and https:// URLs only."
    name = os.path.basename(parsed.path.rstrip("/"))
    if name.endswith(".git"):
        name = name[:-4]
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,60}", name):
        return "Cannot derive a repository directory from this URL."
    if os.path.exists(os.path.join(FS_ROOT, name)):
        return "Directory /%s already exists." % name
    out = run_cmd(["git", "clone", "--depth", "1", "--quiet", url, name], 120)
    if out.startswith(("Failed", "Command", "fatal:")) or "fatal:" in out:
        return out
    count = sum(len(files) for _, _, files in os.walk(os.path.join(FS_ROOT, name)))
    return "Cloned into /%s\n%d files checked out." % (name, count)


def pip_install(spec: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_.-]+(==[A-Za-z0-9_.-]+)?", spec):
        return "Invalid package name. Use letters, digits, . _ - and optional ==version."
    name = spec.split("==", 1)[0]
    os.makedirs(SITE_DIR, exist_ok=True)
    out = run_cmd([sys.executable, "-m", "pip", "install",
                   "--disable-pip-version-check", "--quiet",
                   "--target", SITE_DIR, spec], 180)
    lowered = out.lower()
    if any(word in lowered for word in ("error", "failed", "timed out", "not found")):
        return "Install failed:\n" + out[:600]
    try:
        canonical, version, summary = pypi_info(name)
    except Exception as exc:  # noqa: BLE001 - metadata is optional
        canonical, version, summary = name, "?", "metadata unavailable: %s" % exc
    db = pkg_load()
    db[canonical] = {"version": version, "summary": summary}
    pkg_save(db)
    return ("Installed %s %s\n  %s\n  Files: /site-packages"
            % (canonical, version, summary))


def pip_uninstall(name: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,60}", name):
        return "Invalid package name."
    db = pkg_load()
    key = next((k for k in db if k.lower() == name.lower()), None)
    if key is None:
        return "Package '%s' is not installed." % name
    norm = key.lower().replace("-", "_")
    if os.path.isdir(SITE_DIR):
        for entry in os.listdir(SITE_DIR):
            if entry.lower().replace("-", "_").startswith(norm):
                target = os.path.join(SITE_DIR, entry)
                if os.path.isdir(target):
                    shutil.rmtree(target, ignore_errors=True)
    del db[key]
    pkg_save(db)
    return "Removed %s and its files." % key


NOTES_FILE = os.path.join(FS_ROOT, "notes.txt")


def notes_save(encoded: str) -> str:
    text = encoded.replace("\\n", "\n")
    with open(NOTES_FILE, "w", encoding="utf-8") as fh:
        fh.write(text)
    return "Saved %d bytes to /notes.txt" % len(text.encode("utf-8"))


def notes_load() -> str:
    try:
        with open(NOTES_FILE, "r", encoding="utf-8") as fh:
            text = fh.read(4000)
    except FileNotFoundError:
        return ""
    return text.replace("\n", "\\n")


def shell_command(line: str) -> str:
    parts = line.split()
    cmd = parts[0] if parts else ""
    arg = parts[1] if len(parts) > 1 else ""
    if cmd == "FSLIST":
        return fs_list(arg)
    if cmd == "FSCAT":
        return fs_cat(arg)
    if cmd == "FSMKDIR":
        return fs_mkdir(arg)
    if cmd == "FSEXIST":
        return "YES" if os.path.exists(fs_path(arg)) else "NO"
    if cmd == "FSWRITE":
        return fs_write(arg, " ".join(parts[2:]))
    if cmd == "FSTOUCH":
        return fs_touch(arg)
    if cmd == "FSRM":
        return fs_remove(arg)
    if cmd == "FSMV":
        return fs_move(arg, parts[2] if len(parts) > 2 else "")
    if cmd == "FSCP":
        return fs_copy(arg, parts[2] if len(parts) > 2 else "")
    if cmd == "FSDU":
        return fs_du(arg)
    if cmd == "FSTREE":
        return fs_tree(arg)
    if cmd == "FSHEAD":
        return fs_lines(arg)
    if cmd == "FSTAIL":
        return fs_lines(arg, True)
    if cmd == "FSWC":
        return fs_wc(arg)
    if cmd == "GITCLONE":
        return git_clone(" ".join(parts[1:]))
    if cmd == "GITSTATUS":
        path = git_repo_path(arg)
        if path is None:
            return "Not a git repository: %s" % (arg or "/")
        return run_cmd(["git", "-C", path, "status", "--short", "--branch"], 20)
    if cmd == "GITLOG":
        path = git_repo_path(arg)
        if path is None:
            return "Not a git repository: %s" % (arg or "/")
        return run_cmd(["git", "-C", path, "log", "--oneline", "-8"], 20)
    if cmd in ("PIPINSTALL", "PKGINSTALL"):
        return pip_install(" ".join(parts[1:]))
    if cmd in ("PIPUNINSTALL", "PKGUNINSTALL"):
        return pip_uninstall(arg)
    if cmd == "PIPSHOW":
        try:
            name, version, summary = pypi_info(arg)
        except Exception as exc:  # noqa: BLE001
            return "Cannot show %s: %s" % (arg, exc)
        return "%s %s\n  %s" % (name, version, summary)
    if cmd == "APTUPDATE":
        return apt_update()
    if cmd == "APTSEARCH":
        return apt_search(" ".join(parts[1:]))
    if cmd == "APTINFO":
        return apt_info(arg)
    if cmd == "APTINSTALL":
        return apt_install(parts[1:])
    if cmd == "APTREMOVE":
        return apt_remove(parts[1:])
    if cmd == "APTLIST":
        return apt_list()
    if cmd == "APTFILES":
        return apt_files(arg)
    if cmd == "APTUPGRADE":
        return apt_upgrade(parts[1:])
    if cmd == "APTCLEAN":
        return apt_clean()
    if cmd == "WHICH":
        return apt_which(arg)
    if cmd == "EXEC":
        return run_installed(parts[1:])
    if cmd == "EXECCD":
        return run_installed(parts[2:], parts[1] if len(parts) > 1 else "/")
    return "Unknown shell command: %s" % cmd


def serve_one(conn: socket.socket) -> None:
    buf = b""
    while True:
        try:
            chunk = conn.recv(1)
        except OSError:
            return
        if not chunk:
            return
        if chunk == b"\n":
            line = buf.decode("latin-1", errors="replace").strip()
            buf = b""
            if not line:
                continue
            if line[:4].upper() == "GET ":
                url = line[4:]
            else:
                url = line
            cmd = url.split(" ", 1)[0]
            host_commands = (
                "PKGLIST", "PKGINSTALL", "PKGUNINSTALL", "PIPINSTALL",
                "PIPUNINSTALL", "PIPSHOW", "FSLIST", "FSCAT", "FSMKDIR",
                "FSEXIST", "FSWRITE", "FSTOUCH", "FSRM", "FSMV", "FSCP",
                "FSDU", "FSTREE", "FSHEAD", "FSTAIL", "FSWC",
                "GITCLONE", "GITSTATUS", "GITLOG",
                "APTUPDATE", "APTSEARCH", "APTINFO", "APTINSTALL",
                "APTREMOVE", "APTLIST", "APTFILES", "APTUPGRADE",
                "APTCLEAN", "WHICH", "EXEC", "EXECCD",
            )
            try:
                if cmd == "PKGLIST":
                    body = pkg_command(url)
                elif cmd == "NOTESSAVE":
                    body = notes_save(url[len("NOTESSAVE "):] if " " in url else "")
                elif cmd == "NOTESLOAD":
                    body = notes_load()
                elif cmd in host_commands:
                    body = shell_command(url)
                else:
                    body = fetch(url)
            except Exception as exc:  # noqa: BLE001 - показываем ошибку в браузере
                print("nethelper: %s -> %s" % (url, exc), file=sys.stderr, flush=True)
                body = error_page(url, exc)

            data = body.encode("latin-1", errors="replace")[:MAX_BODY]
            try:
                conn.sendall(struct.pack("<I", len(data)) + data)
            except OSError:
                return
        else:
            if len(buf) < 1024:
                buf += chunk


def main() -> None:
    os.makedirs(FS_ROOT, exist_ok=True)
    os.makedirs(SITE_DIR, exist_ok=True)
    try:
        os.unlink(SOCK)
    except FileNotFoundError:
        pass

    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(SOCK)
    srv.listen(1)
    print("nethelper: жду QEMU на %s" % SOCK, flush=True)

    try:
        while True:
            conn, _ = srv.accept()
            with conn:
                conn.settimeout(340)
                serve_one(conn)
            print("nethelper: соединение закрыто, жду снова", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()
        try:
            os.unlink(SOCK)
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    main()
