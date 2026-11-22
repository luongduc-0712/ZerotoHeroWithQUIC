#!/usr/bin/env python3
"""Real local QUIC/TLS exchanges, failure paths and UDP loss recovery."""
import contextlib
import json
from pathlib import Path
import queue
import re
import selectors
import signal
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
    def __init__(self, server_port, *, initial_loss=True, data_loss=False):
        self.server = ("127.0.0.1", server_port)
        self.client = None
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.05)
        self.port = self.sock.getsockname()[1]
        self.initial_loss = initial_loss
        self.data_loss = data_loss
        self.dropped = set()
        self.short_packets = {"client": 0, "server": 0}
        self.data_dropped = {"client": 0, "server": 0}
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
            if self.initial_loss and direction not in self.dropped:
                self.dropped.add(direction)
                continue
            # QUIC's short header identifies encrypted 1-RTT traffic. Spread
            # bounded losses through a long stream, including after its ring
            # buffer wraps, so retransmission must retain the original bytes.
            if self.data_loss and data and data[0] & 0xC0 == 0x40:
                self.short_packets[direction] += 1
                if self.short_packets[direction] in (73, 809, 1459):
                    self.data_dropped[direction] += 1
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


def exchange(name, client_args, *, failure=False, loss=False, data_loss=False, junk=False, timeout=8):
    port = free_port()
    proc = subprocess.Popen(
        [SERVER, "--port", str(port), "--cert", CERT, "--key", KEY, "--timeout", str(timeout)],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    prefix = ""
    server_output = []
    reader = None
    try:
        prefix = wait_ready(proc)
        # A long sensor run can fill a pipe while the client is still running.
        # Drain server logs concurrently so the test never throttles QUIC I/O.
        reader = threading.Thread(target=lambda: server_output.append(proc.stdout.read()), daemon=True)
        reader.start()
        if junk:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.sendto(bytes(1200), ("127.0.0.1", port))
        relay_context = LossRelay(port, initial_loss=loss, data_loss=data_loss) if loss or data_loss \
            else contextlib.nullcontext()
        with relay_context as relay:
            target = relay.port if relay else port
            result = subprocess.run(
                [CLIENT, "--port", str(target), "--ca", CERT, "--timeout", str(timeout), *client_args],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=12,
            )
            proc.wait(timeout=10)
            reader.join(timeout=2)
            assert not reader.is_alive(), "Server log reader did not finish"
            output = prefix + "".join(server_output)
            if failure:
                assert result.returncode != 0, result.stdout
                assert "ECHO VERIFIED" not in result.stdout, result.stdout
                assert proc.returncode != 0, output
            else:
                assert result.returncode == 0, result.stdout + output
                assert proc.returncode == 0, output + result.stdout
                assert "ECHO VERIFIED" in result.stdout, result.stdout
                assert "result=PASS" in output, output
                if loss:
                    assert relay.dropped == {"client", "server"}
                if data_loss:
                    assert all(count > 0 for count in relay.data_dropped.values()), relay.data_dropped
        print(f"PASS {name}", flush=True)
        return result.stdout, output
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        if reader is not None:
            reader.join(timeout=2)
        proc.stdout.close()


def check_sensor_output(client_output, server_output, count, *, interval_ms=None):
    records = [json.loads(line[len("[sensor] "):])
               for line in client_output.splitlines() if line.startswith("[sensor] {")]
    assert len(records) == count, client_output
    assert [record["seq"] for record in records] == list(range(1, count + 1)), records
    for record in records:
        assert isinstance(record["timestamp_ms"], int), record
        assert abs(record["timestamp_ms"] - int(time.time() * 1000)) < 60000, record
        assert isinstance(record["temperature_c"], (int, float)), record
        assert isinstance(record["humidity_pct"], (int, float)), record
        assert -50 <= record["temperature_c"] <= 100, record
        assert 0 <= record["humidity_pct"] <= 100, record
    if interval_ms is not None:
        for previous, current in zip(records, records[1:]):
            assert current["timestamp_ms"] - previous["timestamp_ms"] >= interval_ms - 2, records
    verified = re.findall(r"\[sensor\] echo verified seq=(\d+)", client_output)
    assert [int(seq) for seq in verified] == list(range(1, count + 1)), client_output
    assert re.search(rf"sensor_generated={count}\b", client_output), client_output
    assert re.search(rf"sensor_verified={count}\b", client_output), client_output
    assert client_output.count("handshake complete") == 1, client_output
    assert server_output.count("handshake complete") == 1, server_output
    assert "stream=0" in client_output and "stream=0" in server_output
    assert "result=PASS" in client_output and "result=PASS" in server_output
    return records


def sensor_interrupted():
    """SIGINT stops generation, then drains and verifies the last pending sample."""
    port = free_port()
    server = subprocess.Popen(
        [SERVER, "--port", str(port), "--cert", CERT, "--key", KEY, "--timeout", "8"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    client = None
    threads = []
    server_output, client_output = [], []
    lines = queue.Queue()
    try:
        prefix = wait_ready(server)
        server_reader = threading.Thread(target=lambda: server_output.append(server.stdout.read()), daemon=True)
        server_reader.start()
        threads.append(server_reader)
        client = subprocess.Popen(
            [CLIENT, "--port", str(port), "--ca", CERT, "--timeout", "8",
             "--sensor", "--interval-ms", "30", "--count", "0"],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )

        def read_client():
            for line in client.stdout:
                client_output.append(line)
                lines.put(line)
            lines.put(None)

        client_reader = threading.Thread(target=read_client, daemon=True)
        client_reader.start()
        threads.append(client_reader)
        generated = 0
        deadline = time.monotonic() + 6
        while generated < 3:
            remaining = deadline - time.monotonic()
            assert remaining > 0, "Sensor did not generate three samples"
            try:
                line = lines.get(timeout=remaining)
            except queue.Empty:
                raise AssertionError("Sensor did not generate three samples") from None
            assert line is not None, "".join(client_output)
            if line.startswith("[sensor] {"):
                generated += 1
        client.send_signal(signal.SIGINT)
        client.wait(timeout=10)
        server.wait(timeout=10)
        for thread in threads:
            thread.join(timeout=2)
            assert not thread.is_alive(), "Sensor log reader did not finish"
        client_text = "".join(client_output)
        server_text = prefix + "".join(server_output)
        assert client.returncode == 0 and server.returncode == 0, client_text + server_text
        count = sum(line.startswith("[sensor] {") for line in client_output)
        assert count >= 3, client_text
        check_sensor_output(client_text, server_text, count)
        print("PASS indefinite sensor drains and verifies pending data on SIGINT", flush=True)
    finally:
        for proc in (client, server):
            if proc is not None:
                if proc.poll() is None:
                    proc.kill()
                proc.wait()
        for thread in threads:
            thread.join(timeout=2)
        for proc in (client, server):
            if proc is not None:
                proc.stdout.close()


def main():
    if not Path(CERT).exists() or not Path(KEY).exists():
        raise SystemExit("Run make certs before make test")
    exchange("message + ignored malformed Initial", ["--message", "Hello QUIC testbed"], junk=True)
    exchange("empty stream with FIN", ["--message", ""])
    client_output, server_output = exchange(
        "sensor sends periodic JSON samples through one QUIC stream",
        ["--sensor", "--interval-ms", "50", "--count", "3"],
    )
    check_sensor_output(client_output, server_output, 3, interval_ms=50)
    started = time.monotonic()
    client_output, server_output = exchange(
        "sensor and server keep an active stream past the inactivity timeout",
        ["--sensor", "--interval-ms", "250", "--count", "7"], timeout=1,
    )
    check_sensor_output(client_output, server_output, 7, interval_ms=250)
    assert time.monotonic() - started > 1, client_output
    client_output, server_output = exchange(
        "sensor survives Initial loss in BOTH directions",
        ["--sensor", "--interval-ms", "20", "--count", "3"], loss=True,
    )
    check_sensor_output(client_output, server_output, 3, interval_ms=20)
    client_output, server_output = exchange(
        "sensor crosses 64 KiB ring wrap with 1-RTT packet loss in BOTH directions",
        ["--sensor", "--interval-ms", "1", "--count", "1600"],
        data_loss=True,
    )
    check_sensor_output(client_output, server_output, 1600)
    transmitted = re.search(r"app_tx=(\d+)", client_output)
    assert transmitted and int(transmitted[1]) > 65536, client_output
    sensor_interrupted()
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
                 ["--message", "x", "--file", "anything"],
                 ["--count", "3"], ["--interval-ms", "30"],
                 ["--sensor", "--message", "x"], ["--sensor", "--file", "anything"],
                 ["--sensor", "--output", "anything"],
                 ["--sensor", "--interval-ms", "0"], ["--sensor", "--interval-ms", "60001"],
                 ["--sensor", "--count", "-1"], ["--sensor", "--count", "1000001"],
                 ["--sensor", "--count", "bad"]):
        result = subprocess.run([CLIENT, *args], cwd=ROOT, capture_output=True, text=True, timeout=2)
        assert result.returncode != 0
    print("PASS invalid arguments rejected", flush=True)
    print("ALL INTEGRATION TESTS PASSED", flush=True)


if __name__ == "__main__":
    main()
