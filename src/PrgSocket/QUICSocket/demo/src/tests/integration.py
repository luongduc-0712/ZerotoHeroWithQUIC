#!/usr/bin/env python3
"""Real local QUIC/TLS exchanges, failure paths and UDP loss recovery."""
import contextlib
from pathlib import Path
import selectors
import socket
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
SERVER = str(ROOT / "bin/quic-server")
CLIENT = str(ROOT / "bin/quic-client")
CERT = str(ROOT / "certs/server.crt")
KEY = str(ROOT / "certs/server.key")


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_ready(proc):
    with selectors.DefaultSelector() as sel:
        sel.register(proc.stdout, selectors.EVENT_READ)
        if not sel.select(5):
            raise AssertionError("Server did not become ready")
    line = proc.stdout.readline()
    if "listening" not in line:
        raise AssertionError(f"Server startup failed: {line}")
    return line


class LossRelay:
    def __init__(self, server_port):
        self.server = ("127.0.0.1", server_port)
        self.client = None
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.05)
        self.port = self.sock.getsockname()[1]
        self.dropped = set()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        while not self.stop.is_set():
            try:
                data, source = self.sock.recvfrom(65536)
            except socket.timeout:
                continue
            except OSError:
                return
            direction = "server" if source == self.server else "client"
            if direction == "client":
                if self.client is not None and source != self.client:
                    continue
                self.client = source
            if direction not in self.dropped:
                self.dropped.add(direction)
                continue
            target = self.client if direction == "server" else self.server
            if target:
                self.sock.sendto(data, target)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.stop.set()
        self.thread.join(timeout=1)
        self.sock.close()


def exchange(name, client_args, *, failure=False, loss=False, junk=False):
    port = free_port()
    proc = subprocess.Popen(
        [SERVER, "--port", str(port), "--cert", CERT, "--key", KEY, "--timeout", "8"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    prefix = ""
    try:
        prefix = wait_ready(proc)
        if junk:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.sendto(bytes(1200), ("127.0.0.1", port))
        with LossRelay(port) if loss else contextlib.nullcontext() as relay:
            target = relay.port if relay else port
            result = subprocess.run(
                [CLIENT, "--port", str(target), "--ca", CERT, "--timeout", "8", *client_args],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=12,
            )
            output, _ = proc.communicate(timeout=10)
            output = prefix + output
            if failure:
                assert result.returncode != 0, result.stdout
                assert "ECHO VERIFIED" not in result.stdout, result.stdout
                assert proc.returncode != 0, output
            else:
                assert result.returncode == 0, result.stdout + output
                assert proc.returncode == 0, output + result.stdout
                assert "ECHO VERIFIED" in result.stdout, result.stdout
                assert "result=PASS" in output, output
                if relay:
                    assert relay.dropped == {"client", "server"}
        print(f"PASS {name}", flush=True)
        return result.stdout, output
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.communicate()


def main():
    if not Path(CERT).exists() or not Path(KEY).exists():
        raise SystemExit("Run make certs before make test")
    exchange("message + ignored malformed Initial", ["--message", "Hello QUIC testbed"], junk=True)
    exchange("empty stream with FIN", ["--message", ""])
    with tempfile.TemporaryDirectory(prefix="quic-testbed-") as temporary:
        folder = Path(temporary)
        source, target = folder / "payload.bin", folder / "echo.bin"
        payload = bytes(range(256)) * 256
        source.write_bytes(payload)
        exchange("64 KiB binary across many QUIC packets", ["--file", str(source), "--output", str(target)])
        assert target.read_bytes() == payload
        exchange("PTO retransmission after Initial loss in BOTH directions",
                 ["--file", str(source)], loss=True)
        source.write_bytes(payload + b"x")
        result = subprocess.run([CLIENT, "--file", str(source)], cwd=ROOT, capture_output=True, text=True)
        assert result.returncode != 0 and "exceeds" in result.stderr
        print("PASS oversized input rejected", flush=True)
        other = folder / "wrong-ca.crt"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=other-ca", "-keyout", str(folder / "other.key"),
                        "-out", str(other)], check=True, capture_output=True)
        exchange("untrusted server certificate rejected", ["--ca", str(other)], failure=True)
    exchange("incorrect certificate identity rejected", ["--server-name", "wrong.invalid"], failure=True)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as blackhole:
        blackhole.bind(("127.0.0.1", 0))
        start = time.monotonic()
        result = subprocess.run([CLIENT, "--ca", CERT, "--port", str(blackhole.getsockname()[1]),
                                 "--timeout", "1"], cwd=ROOT, capture_output=True, text=True, timeout=4)
        assert result.returncode != 0 and time.monotonic() - start < 3
        print("PASS client timeout without a QUIC response", flush=True)
    result = subprocess.run([SERVER, "--port", str(free_port()), "--cert", CERT,
                             "--key", KEY, "--timeout", "1"], cwd=ROOT,
                            capture_output=True, text=True, timeout=4)
    assert result.returncode != 0 and "No valid QUIC" in result.stderr
    print("PASS server timeout without a client", flush=True)
    for args in (["--port", "0"], ["--timeout", "0"], ["--host", "bad-address"],
                 ["--message", "x", "--file", "anything"]):
        result = subprocess.run([CLIENT, *args], cwd=ROOT, capture_output=True, text=True, timeout=2)
        assert result.returncode != 0
    print("PASS invalid arguments rejected", flush=True)
    print("ALL INTEGRATION TESTS PASSED", flush=True)


if __name__ == "__main__":
    main()
