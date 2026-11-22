#!/usr/bin/env python3
"""Read-only IPv4 collision checks before creating the Mininet gateway."""

import argparse
import ipaddress
import json
import shutil
import subprocess
import sys


DEFAULT_SUBNET = "10.77.0.0/24"
PRIVATE_NETWORKS = tuple(ipaddress.ip_network(value) for value in
                         ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))


def parse_subnet(value):
    """Keep the requested .11, .12 and .254 host-address scheme unambiguous."""
    try:
        subnet = ipaddress.IPv4Network(value, strict=True)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    if subnet.prefixlen != 24 or not any(subnet.subnet_of(block) for block in PRIVATE_NETWORKS):
        raise argparse.ArgumentTypeError("use an aligned private IPv4 /24 subnet")
    return subnet


def read_json(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=10)
    if result.returncode:
        raise RuntimeError(f"Cannot inspect {' '.join(command)}: {result.stderr.strip()}")
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"Invalid JSON from {' '.join(command)}") from error


def find_conflicts(subnet, addresses, routes, docker_networks):
    conflicts = []
    for interface in addresses:
        for address in interface.get("addr_info", []):
            if address.get("family") != "inet":
                continue
            assigned = ipaddress.ip_network(
                f"{address['local']}/{address['prefixlen']}", strict=False)
            if subnet.overlaps(assigned):
                conflicts.append(f"interface {interface['ifname']}: {assigned}")
    for route in routes:
        destination = route.get("dst", "default")
        if destination == "default":
            continue
        try:
            routed = ipaddress.ip_network(destination, strict=False)
        except ValueError:
            continue
        if routed.version == 4 and subnet.overlaps(routed):
            conflicts.append(f"route {routed} via {route.get('dev', route.get('gateway', '?'))}")
    for network in docker_networks:
        for config in (network.get("IPAM") or {}).get("Config") or []:
            value = config.get("Subnet")
            if not value:
                continue
            assigned = ipaddress.ip_network(value, strict=False)
            if assigned.version == 4 and subnet.overlaps(assigned):
                conflicts.append(f"Docker network {network['Name']}: {assigned}")
    return sorted(set(conflicts))


def inspect(subnet):
    if shutil.which("ip") is None:
        raise RuntimeError("Missing ip command (iproute2)")
    addresses = read_json(["ip", "-json", "-4", "address", "show"])
    routes = read_json(["ip", "-json", "-4", "route", "show", "table", "all"])
    if shutil.which("docker") is None:
        raise RuntimeError("Missing docker; cannot check Docker IPAM subnet collisions")
    network_ids = subprocess.run(
        ["docker", "network", "ls", "--quiet"], text=True, capture_output=True, timeout=10)
    if network_ids.returncode:
        raise RuntimeError("Cannot inspect Docker networks: " + network_ids.stderr.strip())
    ids = network_ids.stdout.split()
    networks = read_json(["docker", "network", "inspect", *ids]) if ids else []
    return find_conflicts(subnet, addresses, routes, networks)


def require_clear(subnet):
    conflicts = inspect(subnet)
    if conflicts:
        raise RuntimeError(f"Subnet {subnet} is already in use; no network changed:\n  "
                           + "\n  ".join(conflicts)
                           + "\nChoose an unused /24 explicitly with --subnet; do not remove"
                           " unrelated VM/Docker interfaces.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--subnet", type=parse_subnet, default=parse_subnet(DEFAULT_SUBNET))
    parser.add_argument("--json", action="store_true", help="print a machine-readable report")
    args = parser.parse_args()
    try:
        conflicts = inspect(args.subnet)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    report = {"subnet": str(args.subnet), "clear": not conflicts, "conflicts": conflicts}
    if args.json:
        print(json.dumps(report, indent=2))
    elif conflicts:
        print(f"CONFLICT: {args.subnet} is already in use; no network changed.")
        print("\n".join("  " + item for item in conflicts))
    else:
        print(f"CLEAR: {args.subnet} does not overlap current interfaces, routes or Docker IPAM.")
    return 1 if conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
