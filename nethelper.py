#!/usr/bin/env python3
"""
nethelper.py - сетевой мост для TINY BROWSER.

QEMU подключает COM1 к UNIX-сокету. Ядро шлёт "GET <url>\n", помощник
реально загружает страницу через urllib и отвечает:

    u32 (little-endian) длина + тело страницы (latin-1, 1 байт = 1 символ)

Так 20 КБ ядро получает настоящий интернет без собственного TCP/IP-стека.
"""
import os
import socket
import struct
import sys
import urllib.error
import urllib.parse
import urllib.request

SOCK = "/tmp/tinyos-net.sock"
MAX_BODY = 60000
TIMEOUT = 15


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
            "User-Agent": "TinyOS/1.0 (+tiny browser)",
            "Accept": "text/html,text/plain,*/*",
        },
    )
    with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
        data = resp.read(MAX_BODY)

    # latin-1 сохраняет каждый байт как один символ - шрифт ядра 8-битный.
    return data.decode("latin-1", errors="replace")


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
            try:
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
                conn.settimeout(60)
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
