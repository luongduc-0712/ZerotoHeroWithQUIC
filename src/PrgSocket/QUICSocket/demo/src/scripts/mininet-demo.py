#!/usr/bin/env python3
"""Run the QUIC sensor demo in a controller-free h1--s1--h2 network."""

import argparse
import math
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def integer_between(low, high):
    def parse(value):
        try:
            result = int(value)
        except ValueError as error:
            raise argparse.ArgumentTypeError("must be an integer") from error
        if not low <= result <= high:
            raise argparse.ArgumentTypeError(f"must be {low}..{high}")
        return result
    return parse


def real_between(low, high):
    def parse(value):
        try:
            result = float(value)
        except ValueError as error:
            raise argparse.ArgumentTypeError("must be a number") from error
        if not math.isfinite(result) or not low <= result <= high:
            raise argparse.ArgumentTypeError(f"must be {low}..{high}")
        return result
    return parse


def options():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=integer_between(0, 1000000), default=20,
                        help="sensor samples (default: 20); 0 allowed only with --cli")
    parser.add_argument("--interval-ms", type=integer_between(1, 60000), default=1000,
                        help="sample interval in milliseconds (default: 1000)")
    parser.add_argument("--timeout", type=integer_between(1, 3600),
                        help="inactivity watchdog in seconds (default: max(15, interval + 5))")
    parser.add_argument("--max-runtime", type=real_between(1, 604800),
                        help="automatic run wall-clock limit in seconds "
                             "(default: twice sample duration + twice watchdog + 5)")
    parser.add_argument("--bw", type=real_between(0.01, 1000), default=10,
                        help="Mbps on EACH link, in both directions (default: 10)")
    parser.add_argument("--delay-ms", type=real_between(0, 10000), default=0,
                        help="one-way delay on EACH link (default: 0)")
    parser.add_argument("--loss", type=real_between(0, 100), default=0,
                        help="packet loss %% on EACH link, each direction (default: 0)")
    parser.add_argument("--server-name", default="localhost",
                        help="certificate identity, independent of destination IP")
    parser.add_argument("--log-dir", type=Path,
                        help="new/empty directory for server.log and client.log")
    parser.add_argument("--cli", action="store_true",
                        help="open Mininet CLI for manual commands instead of auto-running")
    args = parser.parse_args()
    duration = (args.count - 1) * args.interval_ms / 1000
    if args.timeout is None:
        args.timeout = max(15, math.ceil(args.interval_ms / 1000) + 5)
    if args.timeout * 1000 <= args.interval_ms:
        parser.error("--timeout must exceed --interval-ms / 1000")
    if not args.cli and args.count == 0:
        parser.error("automatic runs need a finite --count; use --cli --count 0 for manual streaming")
    if args.max_runtime is None:
        args.max_runtime = max(0, duration) * 2 + args.timeout * 2 + 5
    if not args.server_name:
        parser.error("--server-name must not be empty")
    return args


def preflight():
    if os.geteuid() != 0:
        raise RuntimeError("Mininet needs root/network namespaces. Run: "
                           "sudo /usr/bin/python3 scripts/mininet-demo.py")
    for name in ("mnexec", "ovs-vsctl", "ovs-ofctl", "ip", "tc", "ping"):
        if shutil.which(name) is None:
            raise RuntimeError(f"Missing {name}; see docs/MININET.md for installation")
    for relative in ("bin/quic-server", "bin/quic-client", "certs/server.crt", "certs/server.key"):
        path = ROOT / relative
        if not path.is_file():
            raise RuntimeError(f"Missing {path}; run make and make certs first")
        if relative.startswith("bin/") and not os.access(path, os.X_OK):
            raise RuntimeError(f"Not executable: {path}")
    result = subprocess.run(["ovs-vsctl", "--timeout=3", "show"],
                            capture_output=True, text=True, timeout=5)
    if result.returncode:
        raise RuntimeError("Open vSwitch is unavailable. Start openvswitch-switch first.\n"
                           + result.stderr.strip())
    result = subprocess.run(["ovs-vsctl", "--timeout=3", "br-exists", "s1"],
                            capture_output=True, text=True, timeout=5)
    if result.returncode == 0:
        raise RuntimeError("An OVS bridge named s1 already exists. Stop that Mininet run "
                           "before starting this demo; no existing bridge was removed.")
    if result.returncode != 2:
        raise RuntimeError("Cannot check existing OVS bridges: " + result.stderr.strip())


def commands(args, count=None):
    server = [str(ROOT / "bin/quic-server"), "--host", "10.0.0.2", "--port", "4433",
              "--cert", str(ROOT / "certs/server.crt"), "--key", str(ROOT / "certs/server.key"),
              "--timeout", str(args.timeout)]
    client = [str(ROOT / "bin/quic-client"), "--host", "10.0.0.2", "--port", "4433",
              "--ca", str(ROOT / "certs/server.crt"), "--server-name", args.server_name,
              "--sensor", "--interval-ms", str(args.interval_ms),
              "--count", str(args.count if count is None else count),
              "--timeout", str(args.timeout)]
    return server, client


def stop_process(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def wait_ready(process, path):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        output = path.read_text(errors="replace")
        if "[server] listening" in output:
            return
        if process.poll() is not None:
            raise RuntimeError("Server failed to start:\n" + output)
        time.sleep(0.05)
    raise RuntimeError("Server did not become ready within 5 s; see " + str(path))


def run_automatic(h1, h2, args):
    if args.log_dir is None:
        parent = ROOT / "logs"
        parent.mkdir(exist_ok=True)
        log_dir = Path(tempfile.mkdtemp(prefix="mininet-", dir=parent))
    else:
        log_dir = args.log_dir.expanduser().resolve()
        log_dir.mkdir(parents=True, exist_ok=True)
        if any(log_dir.iterdir()):
            raise RuntimeError("--log-dir must be empty; existing logs were not overwritten")
    server_path, client_path = log_dir / "server.log", log_dir / "client.log"
    print(f"Logs: {log_dir}", flush=True)
    server_command, client_command = commands(args)
    server = client = None
    with server_path.open("w") as server_log, client_path.open("w") as client_log:
        try:
            server = h2.popen(server_command, cwd=str(ROOT), stdout=server_log,
                              stderr=subprocess.STDOUT)
            wait_ready(server, server_path)
            client = h1.popen(client_command, cwd=str(ROOT), stdout=client_log,
                              stderr=subprocess.STDOUT)
            deadline = time.monotonic() + args.max_runtime
            with client_path.open() as view:
                while client.poll() is None:
                    sys.stdout.write(view.read())
                    sys.stdout.flush()
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Client exceeded --max-runtime; see logs")
                    if server.poll() not in (None, 0):
                        raise RuntimeError("Server failed during exchange:\n"
                                           + server_path.read_text(errors="replace"))
                    time.sleep(0.1)
                sys.stdout.write(view.read())
                sys.stdout.flush()
            server.wait(timeout=10)
            client_text = client_path.read_text(errors="replace")
            server_text = server_path.read_text(errors="replace")
            if (client.returncode or server.returncode or "ECHO VERIFIED" not in client_text
                    or "result=PASS" not in client_text or "result=PASS" not in server_text):
                raise RuntimeError(f"QUIC demo failed (client={client.returncode}, "
                                   f"server={server.returncode}). See {log_dir}")
            print("PASS: h1 sensor -> s1 -> h2, QUIC/TLS handshake and exact echo verified.",
                  flush=True)
        finally:
            # Let the sensor stop while the server and network are still alive.
            stop_process(client)
            stop_process(server)


def main():
    args = options()
    net = None
    try:
        preflight()
        try:
            from mininet.cli import CLI
            from mininet.link import TCLink
            from mininet.log import setLogLevel
            from mininet.net import Mininet
            from mininet.node import OVSBridge
        except ImportError as error:
            raise RuntimeError("Mininet Python module unavailable. Use /usr/bin/python3 "
                               "and install mininet; see docs/MININET.md") from error
        setLogLevel("info")
        net = Mininet(controller=None, switch=OVSBridge, link=TCLink,
                      build=False, autoSetMacs=True)
        h1 = net.addHost("h1", ip="10.0.0.1/24")
        h2 = net.addHost("h2", ip="10.0.0.2/24")
        s1 = net.addSwitch("s1")
        link = dict(bw=args.bw, delay=f"{args.delay_ms:g}ms", loss=args.loss)
        net.addLink(h1, s1, **link)
        net.addLink(s1, h2, **link)
        net.build()
        net.start()
        print("h1 (sensor 10.0.0.1) -- s1 (L2 bridge) -- h2 (QUIC server 10.0.0.2)",
              flush=True)
        loss = net.ping([h1, h2], timeout="2")
        if loss:
            print("Ping had packet loss; QUIC result below will determine success.", flush=True)
        if args.cli:
            server_command, client_command = commands(args)
            print("Manual commands (restart server before each new client):", flush=True)
            print("h2 " + shlex.join(server_command) + " > /tmp/quic-h2.log 2>&1 &", flush=True)
            print("h1 " + shlex.join(client_command), flush=True)
            print("h2 cat /tmp/quic-h2.log", flush=True)
            CLI(net)
        else:
            run_automatic(h1, h2, args)
        return 0
    except KeyboardInterrupt:
        print("\nInterrupted; stopping this demo's processes and network.", file=sys.stderr)
        return 130
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    finally:
        if net is not None:
            net.stop()


if __name__ == "__main__":
    raise SystemExit(main())
