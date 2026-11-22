"""Collision checks protect pre-existing host, VM and Docker networks."""

import argparse
import ipaddress
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from preflight import find_conflicts, parse_subnet


class CollisionTests(unittest.TestCase):
    def setUp(self):
        self.subnet = ipaddress.ip_network("10.77.0.0/24")

    def test_detects_existing_vm_bridge_and_connected_route(self):
        addresses = [{"ifname": "virbr-qdata", "addr_info": [
            {"family": "inet", "local": "10.77.0.1", "prefixlen": 24}]}]
        routes = [{"dst": "10.77.0.0/24", "dev": "virbr-qdata"}]
        conflicts = find_conflicts(self.subnet, addresses, routes, [])
        self.assertEqual(len(conflicts), 2)
        self.assertTrue(all("virbr-qdata" in entry for entry in conflicts))

    def test_docker_supernet_overlap_is_detected(self):
        networks = [{"Name": "existing", "IPAM": {"Config": [{"Subnet": "10.0.0.0/8"}]}}]
        self.assertEqual(find_conflicts(self.subnet, [], [], networks),
                         ["Docker network existing: 10.0.0.0/8"])

    def test_default_route_and_ipv6_do_not_conflict(self):
        routes = [{"dst": "default", "dev": "eno1"}, {"dst": "2001:db8::/32"}]
        networks = [{"Name": "v6", "IPAM": {"Config": [{"Subnet": "fd00::/64"}]}}]
        self.assertEqual(find_conflicts(self.subnet, [], routes, networks), [])

    def test_more_specific_host_route_conflicts(self):
        self.assertTrue(find_conflicts(self.subnet, [], [{"dst": "10.77.0.11", "dev": "tun0"}], []))

    def test_unused_subnet_is_clear(self):
        routes = [{"dst": "10.78.0.0/24", "dev": "other"}]
        self.assertEqual(find_conflicts(self.subnet, [], routes, []), [])

    def test_subnet_requires_private_aligned_slash24(self):
        for value in ("10.77.0.1/24", "10.77.0.0/16", "8.8.8.0/24", "127.0.0.0/24", "::/24"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                parse_subnet(value)
        self.assertEqual(str(parse_subnet("10.78.0.0/24")), "10.78.0.0/24")


if __name__ == "__main__":
    unittest.main()
