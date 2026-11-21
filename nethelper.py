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
import urllib.error
import urllib.parse
import urllib.request

SOCK = "/tmp/tinyos-net.sock"
MAX_BODY = 60000
TIMEOUT = 15
BASE = os.path.dirname(os.path.abspath(__file__))
PKG_DB = os.path.join(BASE, "installed_packages.json")
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
    if cmd == "GITCLONE":
        return git_clone(" ".join(parts[1:]))
    if cmd == "GITSTATUS":
        return run_cmd(["git", "-C", fs_path(arg or "."), "status", "--short", "--branch"], 20)
    if cmd == "GITLOG":
        return run_cmd(["git", "-C", fs_path(arg or "."), "log", "--oneline", "-8"], 20)
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
                "FSEXIST", "GITCLONE", "GITSTATUS", "GITLOG",
            )
            try:
                if cmd == "PKGLIST":
                    body = pkg_command(url)
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
                conn.settimeout(200)
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
