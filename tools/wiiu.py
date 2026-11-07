#!/usr/bin/env python3
"""Work on the Wii U from this PC over the network, with no SD card swapping.

Uses what Aroma's plugins offer on the console: Wiiload (send and start an
app, TCP 4299), FTPiiU (the SD card, port 21) and the game's own UDP log
(port 4405). Setup steps: TESTING.md, "Working from the PC".

  tools/wiiu.py find                 look for the console on the local network
  tools/wiiu.py set-ip 192.168.1.50  remember its address
  tools/wiiu.py status               what is reachable
  tools/wiiu.py dev                  build, send, start, then show the live log
  tools/wiiu.py run [FILE]           send a .wuhb and start it (not kept on the card)
  tools/wiiu.py install [FILE]       put a .wuhb on the card for the Wii U Menu
  tools/wiiu.py log [--for S] [--until REGEX]   live log, also saved in logs/
  tools/wiiu.py pull-log [--all]     copy this build's log file (or all) to logs/console/
  tools/wiiu.py ls [PATH]            list a folder on the card
  tools/wiiu.py get PATH [DEST]      copy a file from the card
  tools/wiiu.py put FILE PATH        copy a file to the card

Paths are relative to the SD card's root. The address comes from WIIU_IP or
the git-ignored file .wiiu-console. Only ever talks to that address.
"""
import argparse
import concurrent.futures
import ftplib
import ipaddress
import os
import posixpath
import re
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / ".wiiu-console"
DEFAULT_WUHB = ROOT / "build/wiiu-gx2-release/DreamKartsU.wuhb"
SD_ROOT = "/fs/vol/external01"
APP_DIR = "wiiu/apps/DreamKartsU"
DEVKITPRO = os.environ.get("DEVKITPRO", "/opt/devkitpro")
WIILOAD_PORT = 4299
FTP_PORT = int(os.environ.get("WIIU_FTP_PORT", "21"))
LOG_PORT = int(os.environ.get("WIIU_LOG_PORT", "4405"))


def die(msg):
    sys.exit(f"wiiu: {msg}")


def console_ip(required=True):
    ip = os.environ.get("WIIU_IP")
    if not ip and CONFIG.exists():
        ip = CONFIG.read_text().strip()
    if not ip and required:
        die("no console address: run tools/wiiu.py find, or tools/wiiu.py set-ip ADDRESS")
    return ip


def port_open(ip, port, timeout=1.0):
    try:
        with socket.create_connection((ip, port), timeout=timeout):
            return True
    except OSError:
        return False


# ---- finding the console ----

def local_network():
    out = subprocess.run(["ip", "-4", "-o", "addr", "show", "scope", "global"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.search(r"inet (\S+)", line)
        if m:
            net = ipaddress.ip_interface(m.group(1)).network
            if net.num_addresses <= 1024:
                return net
    die("no local network of a searchable size found; use tools/wiiu.py set-ip ADDRESS")


def cmd_find(args):
    net = local_network()
    print(f"looking for Wiiload (port {WIILOAD_PORT}) on {net} ...")
    hosts = [str(h) for h in net.hosts()]
    with concurrent.futures.ThreadPoolExecutor(64) as pool:
        hits = [h for h, ok in zip(hosts, pool.map(lambda h: port_open(h, WIILOAD_PORT, 0.5), hosts)) if ok]
    if not hits:
        die("nothing found. Is the console on, at the Wii U Menu or in a game, with the Wiiload plugin enabled?")
    print("found:", ", ".join(hits))
    if len(hits) == 1:
        CONFIG.write_text(hits[0] + "\n")
        print(f"saved {hits[0]} in {CONFIG.name}")
    else:
        print("more than one: choose with tools/wiiu.py set-ip ADDRESS")


def cmd_set_ip(args):
    ipaddress.ip_address(args.address)
    CONFIG.write_text(args.address + "\n")
    print(f"saved {args.address} in {CONFIG.name}")


def cmd_status(args):
    ip = console_ip()
    wl, ftp = port_open(ip, WIILOAD_PORT), port_open(ip, FTP_PORT)
    print(f"console {ip}")
    print(f"  Wiiload (send and start apps): {'ok' if wl else 'NOT reachable: is the Wiiload plugin on?'}")
    print(f"  FTPiiU (SD card):              {'ok' if ftp else 'NOT reachable: is the FTPiiU plugin on?'}")
    if ftp:
        with ftp_connect(ip) as f:
            names = sd_names(f, APP_DIR)
        print(f"  {APP_DIR}: {', '.join(sorted(names)) or 'empty'}")
    if not (wl and ftp):
        sys.exit(1)


# ---- the SD card, over FTP ----

def ftp_connect(ip=None):
    ip = ip or console_ip()
    f = ftplib.FTP()
    try:
        f.connect(ip, FTP_PORT, timeout=30)
        f.login()
    except (OSError, ftplib.Error) as e:
        die(f"cannot reach FTPiiU on {ip}:{FTP_PORT} ({e}). Is the plugin on? (L + Down + Minus on the GamePad)")
    return f


def sd_path(path):
    return posixpath.join(SD_ROOT, path.lstrip("/"))


def sd_names(f, path):
    try:
        return [posixpath.basename(n.rstrip("/")) for n in f.nlst(sd_path(path))]
    except ftplib.error_perm:
        return []


def make_dirs(f, path):
    at = SD_ROOT
    for part in path.split("/"):
        at = posixpath.join(at, part)
        try:
            f.mkd(at)
        except ftplib.error_perm:
            pass   # it exists


class Progress:
    def __init__(self, size, what):
        self.size, self.what, self.done, self.shown = size, what, 0, -1
        self.t0 = time.monotonic()

    def add(self, n):
        self.done += n
        pct = self.done * 100 // max(self.size, 1)
        if pct != self.shown and sys.stderr.isatty():
            self.shown = pct
            print(f"\r{self.what} {pct}%", end="", file=sys.stderr, flush=True)

    def end(self):
        secs = max(time.monotonic() - self.t0, 0.001)
        if sys.stderr.isatty():
            print(file=sys.stderr)
        print(f"{self.what}: {self.done / 1e6:.1f} MB in {secs:.1f} s ({self.done / 1e6 / secs:.1f} MB/s)")


def upload(f, local, remote):
    """To a temporary name, then renamed, so a broken transfer never leaves
    half a file under the real name."""
    local = Path(local)
    if not local.is_file():
        die(f"no such file: {local}")
    folder = posixpath.dirname(remote)
    if folder:
        make_dirs(f, folder)
    tmp = remote + ".part"
    p = Progress(local.stat().st_size, f"sending {local.name}")
    with local.open("rb") as src:
        f.storbinary(f"STOR {sd_path(tmp)}", src, 65536, callback=lambda b: p.add(len(b)))
    p.end()
    if posixpath.basename(remote) in sd_names(f, folder):
        f.delete(sd_path(remote))
    f.rename(sd_path(tmp), sd_path(remote))


def download(f, remote, local):
    local = Path(local)
    local.parent.mkdir(parents=True, exist_ok=True)
    with local.open("wb") as out:
        f.retrbinary(f"RETR {sd_path(remote)}", out.write, 65536)
    print(f"{remote} -> {local.relative_to(ROOT) if local.is_relative_to(ROOT) else local} ({local.stat().st_size} bytes)")


def cmd_install(args):
    with ftp_connect() as f:
        upload(f, args.file, f"{APP_DIR}/DreamKartsU.wuhb")
    print("installed: close the game and start it again from the Wii U Menu to use it")


def cmd_ls(args):
    with ftp_connect() as f:
        f.dir(sd_path(args.path))


def cmd_get(args):
    with ftp_connect() as f:
        download(f, args.path, Path(args.dest) if args.dest else ROOT / "logs/console" / posixpath.basename(args.path))


def cmd_put(args):
    with ftp_connect() as f:
        upload(f, args.file, args.path)


def build_number():
    try:
        return int((ROOT / "BUILD_NUMBER").read_text().split()[0])
    except (OSError, ValueError, IndexError):
        return 0


def cmd_pull_log(args):
    with ftp_connect() as f:
        logs = sorted(n for n in sd_names(f, APP_DIR) if re.fullmatch(r"DreamKartsU-build-\d+\.log", n))
        if not args.all:
            want = f"DreamKartsU-build-{build_number():04d}.log"
            if want not in logs:
                die(f"no {want} on the card yet (found: {', '.join(logs) or 'none'}); --all takes them all")
            logs = [want]
        for n in logs:
            download(f, f"{APP_DIR}/{n}", ROOT / "logs/console" / n)


# ---- the live log ----

class LiveLog:
    """The game's UDP lines (testlog.c, sent to everyone on the network):
    printed, and saved in logs/. Lines from anything but the console are
    dropped."""

    def __init__(self, ip):
        self.ip = ip
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("", LOG_PORT))
        self.sock.settimeout(0.25)
        folder = ROOT / "logs"
        folder.mkdir(exist_ok=True)
        self.path = folder / time.strftime("live-%Y%m%d-%H%M%S.txt")
        self.out = self.path.open("a", buffering=1)
        self.lines = 0

    def follow(self, seconds=None, until=None):
        """True if `until` was seen."""
        end = time.monotonic() + seconds if seconds else None
        pat = re.compile(until) if until else None
        buf = ""
        while end is None or time.monotonic() < end:
            try:
                data, addr = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            if self.ip and addr[0] != self.ip:
                continue
            buf += data.decode("utf-8", "replace")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                self.lines += 1
                print(line, flush=True)
                self.out.write(line + "\n")
                if pat and pat.search(line):
                    return True
        return False


def cmd_log(args):
    live = LiveLog(console_ip(required=False))
    print(f"listening on UDP {LOG_PORT}, saving to {live.path.relative_to(ROOT)}; Ctrl-C ends", file=sys.stderr)
    try:
        seen = live.follow(args.for_seconds, args.until)
    except KeyboardInterrupt:
        return
    print(f"{live.lines} lines", file=sys.stderr)
    if args.until and not seen:
        sys.exit(1)


# ---- sending and starting ----

def wiiload(ip, file):
    file = Path(file)
    if not file.is_file():
        die(f"no such file: {file}")
    if not port_open(ip, WIILOAD_PORT, 3):
        die(f"nothing answers on {ip}:{WIILOAD_PORT}: is the console on, with the Wiiload plugin enabled?")
    env = dict(os.environ, WIILOAD=f"tcp:{ip}")
    t0 = time.monotonic()
    tool = os.environ.get("WIILOAD_TOOL", f"{DEVKITPRO}/tools/bin/wiiload")
    done = subprocess.run([tool, str(file)], env=env, stdout=subprocess.DEVNULL)
    if done.returncode:
        die("wiiload failed")
    print(f"sent {file.name} ({file.stat().st_size / 1e6:.1f} MB) in {time.monotonic() - t0:.1f} s")


def cmd_run(args):
    wiiload(console_ip(), args.file)


def cmd_dev(args):
    ip = console_ip()
    env = dict(os.environ, DEVKITPRO=DEVKITPRO)
    cmd = ["make", "-s", "-f", "Makefile.wiiu", f"-j{os.cpu_count()}"]
    if subprocess.run(cmd, cwd=ROOT, env=env).returncode:
        die("build failed")
    print(f"build {build_number()}")
    live = LiveLog(ip)   # listening before the game starts, for its first lines
    wiiload(ip, DEFAULT_WUHB)
    print(f"live log, saving to {live.path.relative_to(ROOT)}; Ctrl-C ends", file=sys.stderr)
    try:
        live.follow(args.for_seconds, args.until)
    except KeyboardInterrupt:
        pass


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("find").set_defaults(fn=cmd_find)
    s = sub.add_parser("set-ip")
    s.add_argument("address")
    s.set_defaults(fn=cmd_set_ip)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    for name, fn in (("run", cmd_run), ("install", cmd_install)):
        s = sub.add_parser(name)
        s.add_argument("file", nargs="?", default=str(DEFAULT_WUHB))
        s.set_defaults(fn=fn)
    for name, fn in (("log", cmd_log), ("dev", cmd_dev)):
        s = sub.add_parser(name)
        s.add_argument("--for", dest="for_seconds", type=float, help="stop after this many seconds")
        s.add_argument("--until", help="stop at a line matching this regex")
        s.set_defaults(fn=fn)
    s = sub.add_parser("pull-log")
    s.add_argument("--all", action="store_true")
    s.set_defaults(fn=cmd_pull_log)
    s = sub.add_parser("ls")
    s.add_argument("path", nargs="?", default=APP_DIR)
    s.set_defaults(fn=cmd_ls)
    s = sub.add_parser("get")
    s.add_argument("path")
    s.add_argument("dest", nargs="?")
    s.set_defaults(fn=cmd_get)
    s = sub.add_parser("put")
    s.add_argument("file")
    s.add_argument("path")
    s.set_defaults(fn=cmd_put)
    args = p.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
