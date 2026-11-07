#!/usr/bin/env python3
"""Check tools/wiiu.py against stand-ins on this PC, no console: a small FTP
server serving a temporary SD card, a TCP listener taking the real wiiload
tool's transfer on port 4299, and UDP log lines. What the console's own
plugins do can only be shown on the console. Exit code is the verdict."""
import os
import shutil
import socket
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = str(ROOT / "tools/wiiu.py")
sd = Path(tempfile.mkdtemp(prefix="wiiu-sd-"))
SD_PREFIX = "/fs/vol/external01"


class Ftp(socketserver.StreamRequestHandler):
    def say(self, s):
        self.wfile.write((s + "\r\n").encode())

    def local(self, arg):
        assert arg.startswith(SD_PREFIX), arg
        return sd / arg[len(SD_PREFIX):].lstrip("/")

    def data(self):
        conn, _ = self.pasv.accept()
        self.pasv.close()
        return conn

    def handle(self):
        self.say("220 ready")
        rename_from = None
        for raw in self.rfile:
            cmd, _, arg = raw.decode().strip().partition(" ")
            cmd = cmd.upper()
            if cmd == "USER": self.say("230 ok")
            elif cmd == "PASS": self.say("230 ok")
            elif cmd == "TYPE": self.say("200 ok")
            elif cmd == "QUIT":
                self.say("221 bye")
                return
            elif cmd == "PASV":
                self.pasv = socket.socket()
                self.pasv.bind(("127.0.0.1", 0))
                self.pasv.listen(1)
                port = self.pasv.getsockname()[1]
                self.say(f"227 ok (127,0,0,1,{port >> 8},{port & 255})")
            elif cmd == "MKD":
                p = self.local(arg)
                if p.exists(): self.say("550 exists")
                else:
                    p.mkdir(parents=True)
                    self.say("257 ok")
            elif cmd == "STOR":
                self.say("150 go")
                conn = self.data()
                with self.local(arg).open("wb") as f:
                    while chunk := conn.recv(65536): f.write(chunk)
                conn.close()
                self.say("226 ok")
            elif cmd == "RETR":
                self.say("150 go")
                conn = self.data()
                conn.sendall(self.local(arg).read_bytes())
                conn.close()
                self.say("226 ok")
            elif cmd in ("NLST", "LIST"):
                p = self.local(arg)
                if not p.is_dir():
                    self.say("550 no such folder")
                    continue
                self.say("150 go")
                conn = self.data()
                conn.sendall("".join(n.name + "\r\n" for n in p.iterdir()).encode())
                conn.close()
                self.say("226 ok")
            elif cmd == "DELE":
                self.local(arg).unlink()
                self.say("250 ok")
            elif cmd == "RNFR":
                rename_from = self.local(arg)
                self.say("350 ok")
            elif cmd == "RNTO":
                rename_from.rename(self.local(arg))
                self.say("250 ok")
            else: self.say("502 no")


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


received = bytearray()
empty_connections = 0


class Wiiload(socketserver.StreamRequestHandler):
    """The real plugin jams on a connection that sends nothing; count them."""
    def handle(self):
        global empty_connections
        got = 0
        while chunk := self.request.recv(65536):
            received.extend(chunk)
            got += len(chunk)
        empty_connections += not got


fails = 0


def check(what, ok, detail=""):
    global fails
    print(f"{what}: {'PASS' if ok else 'FAIL ' + str(detail)}")
    fails += not ok


ftp = Server(("127.0.0.1", 0), Ftp)
threading.Thread(target=ftp.serve_forever, daemon=True).start()
loader = Server(("127.0.0.1", 4299), Wiiload)
threading.Thread(target=loader.serve_forever, daemon=True).start()
env = dict(os.environ, WIIU_IP="127.0.0.1", WIIU_FTP_PORT=str(ftp.server_address[1]), WIIU_LOG_PORT="44050")


def tool(*args):
    return subprocess.run([sys.executable, "-I", TOOL, *args], env=env, capture_output=True, text=True, timeout=60)


try:
    payload = sd / "src.wuhb"
    payload.write_bytes(os.urandom(300000))
    (sd / "wiiu/apps/DreamKartsU").mkdir(parents=True)
    installed = sd / "wiiu/apps/DreamKartsU/DreamKartsU.wuhb"
    installed.write_bytes(b"old")

    r = tool("install", str(payload))
    check("install replaces the old file", r.returncode == 0 and installed.read_bytes() == payload.read_bytes(), r.stderr)
    check("install leaves no partial file", not list(sd.rglob("*.part")))

    r = tool("status")
    check("status sees FTP and the app", r.returncode == 0 and "DreamKartsU.wuhb" in r.stdout, r.stdout + r.stderr)

    number = int((ROOT / "BUILD_NUMBER").read_text().split()[0])
    name = f"DreamKartsU-build-{number:04d}.log"
    (sd / "wiiu/apps/DreamKartsU" / name).write_text("PERF test\n")
    r = tool("pull-log")
    pulled = ROOT / "logs/console" / name
    check("pull-log fetches this build's log", r.returncode == 0 and pulled.read_text() == "PERF test\n", r.stderr)
    pulled.unlink()

    r = tool("run", str(payload))
    time.sleep(0.5)
    check("run sends with the real wiiload tool", r.returncode == 0 and received[:4] == b"HAXX" and len(received) > 1000,
          r.stderr or bytes(received[:16]))

    check("nothing opened an empty connection to Wiiload", empty_connections == 0, f"{empty_connections} of them")

    def send_log():
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for _ in range(20):
            time.sleep(0.2)
            s.sendto(b"[ 1.000] PERF one\nDKU EXIT: done\n", ("127.0.0.1", 44050))

    threading.Thread(target=send_log, daemon=True).start()
    r = tool("log", "--for", "10", "--until", "DKU EXIT: done")
    check("log follows and stops at a line", r.returncode == 0 and "PERF one" in r.stdout, r.stdout + r.stderr)

    saved = sorted((ROOT / "logs").glob("live-*.txt"))
    check("log is saved to a file", bool(saved) and "PERF one" in saved[-1].read_text())
    if saved:
        saved[-1].unlink()
finally:
    shutil.rmtree(sd, ignore_errors=True)
print("test_console_tool: PASS" if not fails else "test_console_tool: FAIL")
sys.exit(1 if fails else 0)
