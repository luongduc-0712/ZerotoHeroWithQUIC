#!/usr/bin/env python3
"""Exercise two independent C clients against the already-running QUIC broker.

This is a host published-port test. It deliberately does not claim Mininet
coverage and does not launch any substitute broker/server or TCP fallback.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
TOPIC = "lab/temperature"
PAYLOAD = "Temperature = 28.5 C"


class TestFailure(RuntimeError):
    pass


def log_text(path: Path) -> str:
    return path.read_text(errors="replace") if path.exists() else ""


def wait_marker(process: subprocess.Popen, path: Path, marker: str, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if marker in log_text(path):
            return
        if process.poll() is not None:
            raise TestFailure(f"Process exited {process.returncode} before {marker!r}; see {path}")
        time.sleep(0.05)
    raise TestFailure(f"Timed out waiting for {marker!r}; see {path}")


def stop_owned(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=14567)
    parser.add_argument("--ca-file", type=Path, default=ROOT / "docker/certs/ca.pem")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "work/host-test")
    parser.add_argument("--keepalive-delay", type=float, default=11.0,
                        help="Seconds between SUBACK and publication (>=10 to exercise keepalive)")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be in 1..65535")
    if not 10 <= args.keepalive_delay <= 13:
        parser.error("--keepalive-delay must be in 10..13 seconds for the 20s client timeout")
    publisher, subscriber = ROOT / "clients/publisher", ROOT / "clients/subscriber"
    for binary in (publisher, subscriber):
        if not binary.is_file() or not os.access(binary, os.X_OK):
            parser.error(f"Build the C clients first: make -C {ROOT / 'clients'}; missing {binary}")
    args.ca_file = args.ca_file.resolve()
    if not args.ca_file.is_file():
        parser.error(f"Missing CA certificate: {args.ca_file}")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {
        "started_at": dt.datetime.now().astimezone().isoformat(),
        "scope": "host-published-UDP-port; not Mininet h1/h2",
        "endpoint": f"{args.host}:{args.port}/udp",
        "server_name": "broker.test",
        "topic": TOPIC,
        "payload": PAYLOAD,
        "cases": [],
        "success": False,
    }
    base = ["--host", args.host, "--port", str(args.port), "--ca-file", str(args.ca_file),
            "--server-name", "broker.test", "--topic", TOPIC]
    owned: list[subprocess.Popen] = []

    def start(binary: Path, arguments: list[str], logfile: Path) -> subprocess.Popen:
        # Logs are observations only: all MQTT application bytes pass through
        # each client's QUIC connection and EMQX, never between these handles.
        with logfile.open("w") as stream:
            proc = subprocess.Popen([str(binary), *arguments], cwd=ROOT, stdout=stream,
                                    stderr=subprocess.STDOUT)
        owned.append(proc)
        return proc

    def expect_zero(proc: subprocess.Popen, logfile: Path, seconds: float) -> None:
        try:
            code = proc.wait(timeout=seconds)
        except subprocess.TimeoutExpired as exc:
            raise TestFailure(f"Process did not exit cleanly; see {logfile}") from exc
        if code != 0:
            raise TestFailure(f"Process exited {code}; see {logfile}")

    try:
        case = {"name": "publish_subscribe_and_keepalive", "success": False}
        report["cases"].append(case)
        sublog, publog = output / "subscriber.log", output / "publisher.log"
        sub = start(subscriber, [*base, "--count", "1", "--timeout", "20"], sublog)
        wait_marker(sub, sublog, "MQTT CONNACK accepted", 15)
        wait_marker(sub, sublog, "MQTT SUBACK accepted", 3)
        ready_at = time.monotonic()
        # Wait for a real PINGRESP before publishing; elapsed time alone is
        # never treated as proof of a working MQTT keepalive exchange.
        wait_marker(sub, sublog, "MQTT PINGRESP received", 14)
        remaining = args.keepalive_delay - (time.monotonic() - ready_at)
        if remaining > 0:
            time.sleep(remaining)
        pub = start(publisher, [*base, "--message", PAYLOAD, "--timeout", "15"], publog)
        expect_zero(pub, publog, 18)
        expect_zero(sub, sublog, 6)
        observed = log_text(sublog)
        expected_line = f"MQTT PUBLISH received topic={TOPIC} payload={PAYLOAD}"
        if observed.splitlines().count(expected_line) != 1:
            raise TestFailure(f"Expected exactly one complete matching topic/payload line; see {sublog}")
        for marker in ("MQTT CONNACK accepted", "MQTT SUBACK accepted", "MQTT PINGREQ sent",
                       "MQTT PINGRESP received", "MQTT DISCONNECT queued"):
            if marker not in observed:
                raise TestFailure(f"Missing {marker!r}; see {sublog}")
        if "MQTT PUBLISH acknowledged by QUIC" not in log_text(publog):
            raise TestFailure(f"Publisher did not prove QUIC stream acknowledgement; see {publog}")
        case.update(success=True, subscriber_exit=sub.returncode, publisher_exit=pub.returncode,
                    subscriber_log=str(sublog), publisher_log=str(publog),
                    keepalive_pingresp=True, exact_payload_received=True)
        print("PASS: QUIC/MQTT publish-subscribe, exact payload, PINGREQ/PINGRESP, clean exits", flush=True)

        for name in ("wrong_server_name", "untrusted_ca"):
            case = {"name": name, "success": False}
            report["cases"].append(case)
            logfile = output / f"{name}.log"
            with tempfile.TemporaryDirectory(prefix="untrusted-ca-", dir=output) as temp:
                negative_base = list(base)
                if name == "wrong_server_name":
                    negative_base[negative_base.index("--server-name") + 1] = "wrong.test"
                else:
                    ca = Path(temp) / "ca.pem"
                    generated = subprocess.run(
                        ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256",
                         "-days", "1", "-subj", "/CN=Untrusted MQTT Test CA",
                         "-addext", "basicConstraints=critical,CA:TRUE",
                         "-keyout", str(Path(temp) / "ca.key"), "-out", str(ca)],
                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, check=False,
                    )
                    if generated.returncode:
                        raise TestFailure(f"Could not generate independent untrusted CA: {generated.stderr}")
                    negative_base[negative_base.index("--ca-file") + 1] = str(ca)
                proc = start(publisher, [*negative_base, "--timeout", "8"], logfile)
                try:
                    code = proc.wait(timeout=12)
                except subprocess.TimeoutExpired as exc:
                    raise TestFailure(f"TLS rejection did not complete; see {logfile}") from exc
            observed = log_text(logfile)
            if code == 0 or "MQTT CONNACK accepted" in observed:
                raise TestFailure(f"TLS negative case incorrectly accepted: {name}; see {logfile}")
            if not re.search(r"TLS_CERTIFICATE_REJECTED|certificate.*(?:fail|reject|invalid|error)|(?:fail|reject|invalid|error).*certificate",
                             observed, re.IGNORECASE):
                raise TestFailure(f"TLS negative case did not show a certificate verification error: {name}; see {logfile}")
            case.update(success=True, exit_code=code, certificate_rejected=True, log=str(logfile))
            print(f"PASS: TLS rejects {name}", flush=True)

        case = {"name": "subscriber_sigint", "success": False}
        report["cases"].append(case)
        logfile = output / "subscriber-sigint.log"
        proc = start(subscriber, [*base, "--count", "0", "--timeout", "15"], logfile)
        wait_marker(proc, logfile, "MQTT SUBACK accepted", 15)
        proc.send_signal(signal.SIGINT)
        expect_zero(proc, logfile, 6)
        observed = log_text(logfile)
        if "Stopping on signal" not in observed or "MQTT DISCONNECT queued" not in observed:
            raise TestFailure(f"SIGINT did not take the controlled MQTT disconnect path; see {logfile}")
        case.update(success=True, exit_code=proc.returncode, log=str(logfile))
        print("PASS: subscriber handles SIGINT with MQTT DISCONNECT and clean exit", flush=True)
        report["success"] = True
        return 0
    except (OSError, TestFailure) as exc:
        report["error"] = str(exc)
        print(f"FAIL: {exc}", flush=True)
        return 1
    finally:
        for proc in owned:
            stop_owned(proc)
        report["finished_at"] = dt.datetime.now().astimezone().isoformat()
        destination = output / "report.json"
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print(f"Actual test report: {destination}", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
