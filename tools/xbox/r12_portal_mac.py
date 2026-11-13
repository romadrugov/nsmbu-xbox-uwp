#!/usr/bin/env python3
"""Xbox Device Portal helper for NSMBU R12 on macOS/Linux (no PowerShell).

Use ONLY on a trusted local network with your own Xbox in Developer Mode.
Device Portal uses a self-signed certificate, hence certificate validation is
disabled for the explicitly selected local console only.
Does not upload games to GitHub or to any server other than your console.
"""
from __future__ import annotations
import argparse
import base64
import getpass
import json
import os
from pathlib import Path
import re
import ssl
import sys
import tempfile
import time
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError
from uuid import uuid4
from zipfile import ZipFile, ZIP_DEFLATED

DEFAULT_PACKAGE = "Romadrugov.NSMBUFullRuntimeR12"
ZIP_RAW_BUDGET = 80 * 1024 * 1024
MAX_PORTAL_POST = 220 * 1024 * 1024


class Portal:
    def __init__(self, host: str, port: int, username: str, password: str):
        if not re.fullmatch(r"[a-zA-Z0-9.:-]+", host):
            raise ValueError("Use the Xbox local IP address, not a URL")
        self.base = f"https://{host}:{port}"
        self.ctx = ssl.create_default_context()
        self.ctx.check_hostname = False  # Xbox uses a self-signed Dev Portal certificate
        self.ctx.verify_mode = ssl.CERT_NONE
        self.auth = "Basic " + base64.b64encode(
            f"{username}:{password}".encode("utf-8")).decode("ascii")
        self.csrf = ""
        self.call("GET", "/api/app/packagemanager/packages", bootstrap=True)

    def call(self, method: str, path: str, data: bytes | None = None,
             content_type: str | None = None, bootstrap: bool = False) -> bytes:
        headers = {"Authorization": self.auth, "Accept": "application/json"}
        if self.csrf:
            headers["X-CSRF-Token"] = self.csrf
            headers["Cookie"] = f"CSRF-Token={self.csrf}"
        if content_type:
            headers["Content-Type"] = content_type
        req = Request(self.base + path, data=data, headers=headers, method=method)
        try:
            with urlopen(req, timeout=180, context=self.ctx) as resp:
                cookies = resp.headers.get_all("Set-Cookie", [])
                if bootstrap:
                    for cookie in cookies:
                        match = re.search(r"CSRF-Token=([^;,\s]+)", cookie)
                        if match:
                            self.csrf = match.group(1)
                    if not self.csrf:
                        raise RuntimeError("Portal did not supply a CSRF token")
                return resp.read()
        except HTTPError as exc:
            payload = exc.read(1500).decode("utf-8", errors="replace")
            raise RuntimeError(f"Portal {method} {path}: HTTP {exc.code}: {payload}") from exc
        except URLError as exc:
            raise RuntimeError(f"Cannot reach {self.base}: {exc}") from exc

    def get_json(self, endpoint: str) -> dict:
        return json.loads(self.call("GET", endpoint).decode("utf-8"))

    def packages(self) -> list[dict]:
        return self.get_json("/api/app/packagemanager/packages").get("InstalledPackages", [])

    def r12_package(self) -> dict:
        found = [p for p in self.packages() if p.get("Name") == DEFAULT_PACKAGE
                 or p.get("PackageFullName", "").startswith(DEFAULT_PACKAGE + "_")]
        if len(found) != 1:
            raise RuntimeError(f"Expected one installed {DEFAULT_PACKAGE}; found {len(found)}")
        return found[0]

    def multipart(self, endpoint: str, files: list[Path], field: str) -> None:
        boundary = "----nsmbu-r12-" + uuid4().hex
        body = bytearray()
        for file in files:
            body += f"--{boundary}\r\n".encode()
            body += (f'Content-Disposition: form-data; name="{field}"; '
                     f'filename="{file.name}"\r\n'
                     f"Content-Type: application/octet-stream\r\n\r\n").encode()
            body += file.read_bytes()
            body += b"\r\n"
        body += f"--{boundary}--\r\n".encode()
        if len(body) > MAX_PORTAL_POST:
            raise ValueError(f"Upload is {len(body) // (1024 * 1024)} MiB; split it into smaller batches")
        self.call("POST", endpoint, bytes(body), f"multipart/form-data; boundary={boundary}")

    def install(self, appx: Path, vclibs: Path | None, cert: Path | None) -> None:
        files = [appx]
        if vclibs:
            files.append(vclibs)
        if cert:
            files.append(cert)
        endpoint = "/api/app/packagemanager/package?" + urlencode({"package": appx.name})
        self.multipart(endpoint, files, "package")
        print("Package upload accepted. Check Xbox Device Portal installation state.")
        for _ in range(40):
            time.sleep(2)
            try:
                state = self.call("GET", "/api/app/packagemanager/state")
                print("Install state:", state.decode("utf-8", errors="replace")[:250])
                break
            except RuntimeError as exc:
                if "HTTP 4" not in str(exc):
                    raise

    def launch(self) -> None:
        p = self.r12_package()
        app_id = p.get("PackageRelativeId")
        full = p.get("PackageFullName")
        if not app_id or not full:
            raise RuntimeError("Xbox did not provide PackageRelativeId/PackageFullName")
        b64 = lambda s: base64.b64encode(s.encode("utf-8")).decode("ascii")
        endpoint = "/api/taskmanager/app?appid=" + quote(b64(app_id), safe="")
        endpoint += "&package=" + quote(b64(full), safe="")
        self.call("POST", endpoint, b"")
        print("Launch request sent to Xbox")

    def folder_files(self, path: str) -> list[dict]:
        pfn = self.r12_package()["PackageFullName"]
        query = urlencode({"knownfolderid": "LocalAppData", "packagefullname": pfn, "path": path})
        return self.get_json("/api/filesystem/apps/files?" + query).get("Items", [])

    def upload_zip(self, archive: Path) -> None:
        pfn = self.r12_package()["PackageFullName"]
        query = urlencode({"knownfolderid": "LocalAppData",
                           "packagefullname": pfn, "path": r"\LocalState", "extract": "true"})
        self.multipart("/api/filesystem/apps/file?" + query, [archive], "file")

    def download_log(self, target: Path) -> None:
        pfn = self.r12_package()["PackageFullName"]
        query = urlencode({"knownfolderid": "LocalAppData", "packagefullname": pfn,
                           "path": r"\LocalState\logs", "filename": "nsmbu.log"})
        data = self.call("GET", "/api/filesystem/apps/file?" + query)
        target.write_bytes(data)
        print("Saved", target, len(data), "bytes")


def game_files(folder: Path) -> list[Path]:
    if not (folder / "code" / "red-pro2.rpx").is_file():
        raise ValueError("Expected code/red-pro2.rpx in the extracted game directory")
    if not (folder / "meta" / "meta.xml").is_file() or not (folder / "content").is_dir():
        raise ValueError("Expected full code/content/meta tree, not a single RPX")
    paths = sorted(p for p in folder.rglob("*") if p.is_file())
    if not paths:
        raise ValueError("Game directory contains no files")
    return paths


def upload_game(portal: Portal, root: Path) -> None:
    files = game_files(root)
    groups: list[list[Path]] = []
    current: list[Path] = []
    used = 0
    for file in files:
        size = file.stat().st_size
        if current and used + size > ZIP_RAW_BUDGET:
            groups.append(current)
            current, used = [], 0
        current.append(file)
        used += size
    if current:
        groups.append(current)
    print(f"Preparing {len(groups)} archives with {len(files)} game files (no GitHub upload).")
    with tempfile.TemporaryDirectory(prefix="nsmbu-r12-") as temp:
        for idx, group in enumerate(groups, 1):
            archive = Path(temp) / f"nsmbu-game-{idx:04d}.zip"
            with ZipFile(archive, "w", compression=ZIP_DEFLATED, compresslevel=2) as zf:
                for f in group:
                    zf.write(f, "game/" + f.relative_to(root).as_posix())
            print(f"Uploading {idx}/{len(groups)}: {archive.stat().st_size // (1024*1024)} MiB")
            portal.upload_zip(archive)
    print("Game tree submitted to Xbox LocalState/game. Confirm files via 'list'.")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", required=True, help="Local IPv4/IPv6 address of Xbox Device Portal")
    ap.add_argument("--port", type=int, default=11443)
    ap.add_argument("--user", default="DevPortal", help="Device Portal username")
    sub = ap.add_subparsers(dest="action", required=True)
    p = sub.add_parser("install")
    p.add_argument("--appx", type=Path, required=True)
    p.add_argument("--vclibs", type=Path)
    p.add_argument("--cer", type=Path)
    g = sub.add_parser("upload-game")
    g.add_argument("--game", type=Path, required=True)
    sub.add_parser("launch")
    p = sub.add_parser("list")
    p.add_argument("--path", default=r"\LocalState")
    p = sub.add_parser("download-log")
    p.add_argument("--out", type=Path, default=Path("nsmbu_r12.log"))
    opts = ap.parse_args()
    password = getpass.getpass(f"Xbox Device Portal password for {opts.user}: ")
    portal = Portal(opts.host, opts.port, opts.user, password)
    if opts.action == "install":
        portal.install(opts.appx.resolve(),
                       opts.vclibs.resolve() if opts.vclibs else None,
                       opts.cer.resolve() if opts.cer else None)
    elif opts.action == "upload-game":
        upload_game(portal, opts.game.expanduser().resolve())
    elif opts.action == "launch":
        portal.launch()
    elif opts.action == "list":
        for item in portal.folder_files(opts.path):
            print(item.get("Name"), item.get("Size"), item.get("Type"))
    elif opts.action == "download-log":
        portal.download_log(opts.out)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError) as exc:
        print("R12 portal error:", exc, file=sys.stderr)
        raise SystemExit(1)
