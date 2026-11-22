#!/usr/bin/env python3
"""h1/h2 -- standalone s1 -- Ubuntu mn-gw -- Docker's published QUIC port."""

import argparse
import fcntl
import os
from pathlib import Path
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from preflight import DEFAULT_SUBNET, parse_subnet, require_clear

OWNER_KEY = "mqtt-quic-owner"
RESERVED_INTERFACES = ("s1", "mn-gw", "h1-eth0", "h2-eth0", "s1-eth1", "s1-eth2")
TOPIC = "lab/temperature"
MESSAGE = "Temperature = 28.5 C"


def checked(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=10)
    if result.returncode:
        raise RuntimeError(f"{' '.join(command)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def ovs(*arguments):
    return checked(["ovs-vsctl", "--timeout=5", *arguments])


def acquire_lock():
    # Never remove the lock file: unlinking a locked inode permits two owners.
    path = "/run/lock/mqtt-quic-mininet.lock"
    descriptor = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    info = os.fstat(descriptor)
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0:
        os.close(descriptor)
        raise RuntimeError(f"Untrusted lock file {path}; root must inspect it")
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as error:
        os.close(descriptor)
        raise RuntimeError("Another mqtt-quic Mininet run already owns s1/mn-gw") from error
    return descriptor


def check_reserved_names():
    checked(["ovs-vsctl", "--timeout=5", "show"])
    bridges = ovs("list-br").splitlines()
    if "s1" in bridges:
        raise RuntimeError("OVS bridge s1 already exists; it was not removed")
    interfaces = set(ovs("--data=bare", "--no-heading", "--columns=name", "list", "Interface").splitlines())
    for name in RESERVED_INTERFACES:
        if name in interfaces or Path("/sys/class/net", name).exists():
            raise RuntimeError(f"Interface {name} already exists; it was not changed")


def require_runtime():
    if os.geteuid() != 0:
        raise RuntimeError("Mininet requires root. After resolving subnet conflicts, run "
                           "sudo /usr/bin/python3 mininet/topology.py [--subnet UNUSED/24] [--test]")
    for command in ("mnexec", "ovs-vsctl", "ip", "ping"):
        if shutil.which(command) is None:
            raise RuntimeError(f"Missing required command: {command}")


def make_network(owner):
    try:
        from mininet.net import Mininet
        from mininet.node import OVSBridge, Switch
    except ImportError as error:
        raise RuntimeError("Mininet module missing; use /usr/bin/python3") from error

    class OwnedBridge(OVSBridge):
        """Avoid OVSSwitch.start's unconditional deletion of an existing bridge."""

        @classmethod
        def batchStartup(cls, switches):
            # Each start() above already completed its checked transaction.
            del switches
            return []

        @classmethod
        def batchShutdown(cls, switches):
            # Mininet otherwise bypasses stop() and deletes bridges unconditionally.
            del switches
            return []

        def start(self, controllers):
            del controllers
            arguments = ["add-br", self.name, "--", "set", "Bridge", self.name,
                         "fail_mode=standalone", "other_config:disable-in-band=true",
                         f"external_ids:{OWNER_KEY}={owner}"]
            for interface in self.intfList():
                if self.ports[interface] and not interface.IP():
                    arguments.extend(["--", "add-port", self.name, interface.name])
            # ovs-vsctl can commit before reporting a timeout; cleanup still checks the owner.
            self.owned = True
            ovs(*arguments)  # Atomic add-br refuses an existing bridge; never del-br here.

        def stop(self, deleteIntfs=True):
            if getattr(self, "owned", False):
                try:
                    current_owner = ovs("--if-exists", "get", "Bridge", self.name,
                                        f"external_ids:{OWNER_KEY}").strip('"')
                    if current_owner == owner:
                        ovs("del-br", self.name)
                    elif current_owner:
                        print("WARNING: s1 ownership changed; bridge left untouched", file=sys.stderr)
                except (RuntimeError, OSError, subprocess.SubprocessError) as error:
                    print(f"WARNING: scoped OVS cleanup failed: {error}", file=sys.stderr)
            Switch.stop(self, deleteIntfs=deleteIntfs)

    # build=False ensures the caller has the network object before any hosts/links exist.
    net = Mininet(controller=None, switch=OwnedBridge, build=False, autoSetMacs=True)
    return net


def client_commands(args):
    gateway = str(args.subnet.network_address + 254)
    common = ["--host", gateway, "--port", "14567", "--ca-file", str(args.ca_file),
              "--server-name", "broker.test", "--topic", TOPIC]
    subscriber = [str(ROOT / "clients/subscriber"), *common, "--count", "1"]
    publisher = [str(ROOT / "clients/publisher"), *common, "--message", MESSAGE]
    return subscriber, publisher


def check_clients(args):
    for name in ("subscriber", "publisher"):
        path = ROOT / "clients" / name
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RuntimeError(f"Missing executable {path}; run make -C clients")
    if not args.ca_file.is_file():
        raise RuntimeError(f"Missing CA certificate {args.ca_file}; generate docker certificates first")


def stop_process(process):
    if process is None or process.poll() is not None:
        return
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=4)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def wait_subscribed(process, log_path, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        output = log_path.read_text(errors="replace")
        if "SUBACK accepted" in output:
            return
        if process.poll() is not None:
            raise RuntimeError("Subscriber stopped before SUBACK:\n" + output)
        time.sleep(0.05)
    raise RuntimeError(f"Subscriber did not receive SUBACK in {timeout:g}s; see {log_path}")


def run_test(h1, h2, args):
    check_clients(args)
    if args.log_dir is None:
        parent = ROOT / "work"
        parent.mkdir(exist_ok=True)
        log_dir = Path(tempfile.mkdtemp(prefix="mininet-", dir=parent))
    else:
        log_dir = args.log_dir.expanduser().resolve()
        log_dir.mkdir(parents=True, exist_ok=True)
        if any(log_dir.iterdir()):
            raise RuntimeError("--log-dir must be empty; existing logs were not overwritten")
    print(f"Test logs: {log_dir}", flush=True)
    subscriber_command, publisher_command = client_commands(args)
    subscriber_path = log_dir / "subscriber.log"
    publisher_path = log_dir / "publisher.log"
    subscriber = publisher = None
    with subscriber_path.open("w") as subscriber_log, publisher_path.open("w") as publisher_log:
        try:
            subscriber = h2.popen(subscriber_command, cwd=str(ROOT), stdout=subscriber_log,
                                  stderr=subprocess.STDOUT)
            wait_subscribed(subscriber, subscriber_path, args.timeout)
            print("MQTT SUBSCRIBE: SUBACK accepted on h2", flush=True)
            publisher = h1.popen(publisher_command, cwd=str(ROOT), stdout=publisher_log,
                                 stderr=subprocess.STDOUT)
            publisher.wait(timeout=args.timeout)
            subscriber.wait(timeout=args.timeout)
            output = subscriber_path.read_text(errors="replace")
            publisher_output = publisher_path.read_text(errors="replace")
            expected = f"MQTT PUBLISH received topic={TOPIC} payload={MESSAGE}"
            if (publisher.returncode or subscriber.returncode or expected not in output
                    or "MQTT PUBLISH acknowledged by QUIC" not in publisher_output):
                raise RuntimeError(f"MQTT delivery failed (publisher={publisher.returncode}, "
                                   f"subscriber={subscriber.returncode}); see {log_dir}")
            print(output, end="" if output.endswith("\n") else "\n", flush=True)
            print("PASS: h1 -> QUIC/ngtcp2 -> EMQX/Docker -> QUIC/ngtcp2 -> h2; "
                  "topic and payload verified.", flush=True)
        finally:
            stop_process(publisher)
            stop_process(subscriber)


def check_ip(h1, h2, gateway):
    for host, target in ((h1, gateway), (h2, gateway), (h1, h2.IP())):
        process = host.popen(["ping", "-c", "3", "-W", "2", target],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            output, _ = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise RuntimeError(f"IP connectivity timed out: {host.name} -> {target}")
        print(output, end="", flush=True)
        if process.returncode:
            raise RuntimeError(f"IP connectivity failed: {host.name} -> {target}")
    print("IP connectivity: PASS. QUIC/TLS and MQTT require the client test.", flush=True)


def options():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--subnet", type=parse_subnet, default=parse_subnet(DEFAULT_SUBNET))
    parser.add_argument("--check-only", action="store_true", help="read-only subnet check; no root needed")
    parser.add_argument("--test", action="store_true", help="run one real MQTT exchange, then clean up")
    parser.add_argument("--timeout", type=float, default=25, help="seconds allowed per client stage")
    parser.add_argument("--log-dir", type=Path, help="new or empty test log directory")
    parser.add_argument("--ca-file", type=Path, default=ROOT / "docker/certs/ca.pem")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 300:
        parser.error("--timeout must be finite and between 1 and 300 seconds")
    args.ca_file = args.ca_file.expanduser().resolve()
    return args


def main():
    args = options()
    def terminate(signum, frame):
        del signum, frame
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, terminate)
    net = None
    lock = None
    owner = uuid.uuid4().hex
    gateway_created = False
    try:
        require_clear(args.subnet)  # Deliberately before root checks and before any writes.
        if args.check_only:
            print(f"CLEAR: {args.subnet}; no network changed")
            return 0
        require_runtime()
        lock = acquire_lock()
        require_clear(args.subnet)  # Recheck after acquiring the lifetime lock.
        check_reserved_names()
        if args.test:
            check_clients(args)
        from mininet.log import setLogLevel
        setLogLevel("info")
        net = make_network(owner)
        h1 = net.addHost("h1", ip=f"{args.subnet.network_address + 11}/24")
        h2 = net.addHost("h2", ip=f"{args.subnet.network_address + 12}/24")
        switch = net.addSwitch("s1")
        net.addLink(h1, switch)
        net.addLink(h2, switch)
        net.build()
        net.start()
        ovs("add-port", "s1", "mn-gw", "--", "set", "Interface", "mn-gw", "type=internal",
            f"external_ids:{OWNER_KEY}={owner}")
        gateway_created = True
        gateway = str(args.subnet.network_address + 254)
        checked(["ip", "address", "add", f"{gateway}/24", "dev", "mn-gw"])
        checked(["ip", "link", "set", "dev", "mn-gw", "up"])
        if ovs("get", "Bridge", "s1", "fail_mode").strip('"') != "standalone":
            raise RuntimeError("s1 is not in standalone forwarding mode")
        print(f"h1={h1.IP()}/24 h2={h2.IP()}/24 mn-gw={gateway}/24 "
              f"EMQX={gateway}:14567/UDP", flush=True)
        check_ip(h1, h2, gateway)
        if args.test:
            run_test(h1, h2, args)
        else:
            from mininet.cli import CLI
            subscriber, publisher = client_commands(args)
            # Interactive subscriber should remain connected after the first publication.
            print("Start subscriber, wait for SUBACK, then publisher:", flush=True)
            print("h2 " + shlex.join(subscriber[:-2]) + " > /tmp/mqtt-quic-h2.log 2>&1 &", flush=True)
            print("h2 cat /tmp/mqtt-quic-h2.log", flush=True)
            print("h1 " + shlex.join(publisher), flush=True)
            CLI(net)
        return 0
    except KeyboardInterrupt:
        print("\nInterrupted; cleaning up this topology only.", file=sys.stderr)
        return 130
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    finally:
        if gateway_created:
            try:
                if ovs("get", "Interface", "mn-gw", f"external_ids:{OWNER_KEY}").strip('"') == owner:
                    ovs("--if-exists", "del-port", "s1", "mn-gw")
                else:
                    print("WARNING: mn-gw ownership changed; port left untouched", file=sys.stderr)
            except (RuntimeError, OSError, subprocess.SubprocessError) as error:
                print(f"WARNING: internal port cleanup: {error}", file=sys.stderr)
        if net is not None:
            net.stop()
        if lock is not None:
            os.close(lock)


if __name__ == "__main__":
    sys.exit(main())
