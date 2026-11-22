"""Exercise the installed Mininet lifecycle without creating interfaces or shells."""

import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

try:
    from mininet.net import Mininet
    from mininet.node import Switch
except ImportError:
    Mininet = Switch = None

path = Path(__file__).resolve().parents[1] / "mininet/topology.py"
spec = importlib.util.spec_from_file_location("mqtt_quic_topology", path)
topology = importlib.util.module_from_spec(spec)
spec.loader.exec_module(topology)


@unittest.skipIf(Mininet is None, "Mininet is not installed")
class InstalledMininetLifecycleTests(unittest.TestCase):
    def run_lifecycle(self, current_owner, fail_start=False):
        def initialize_without_shell(bridge, name, **kwargs):
            del kwargs
            bridge.name = name
            bridge.intfs = {}
            bridge.ports = {}
            bridge.terminate = Mock()

        def ovs_without_side_effects(*arguments):
            if arguments[:2] == ("add-br", "s1") and fail_start:
                raise RuntimeError("ovs-vsctl timed out after committing")
            if arguments[:4] == ("--if-exists", "get", "Bridge", "s1"):
                if current_owner is None:
                    return ""
                return '"' + current_owner + '"'
            return ""

        # Mininet.init tunes host limits; Switch.__init__ starts a namespace shell.
        # Substitute only those side effects, keeping the actual start/stop dispatch.
        with patch.object(Mininet, "init"), patch.object(Switch, "__init__", initialize_without_shell), \
                patch.object(Switch, "stop") as stop_node, \
                patch("sys.stderr", new_callable=io.StringIO) as stderr, \
                patch.object(topology, "ovs", side_effect=ovs_without_side_effects) as ovs:
            net = topology.make_network("this-run")
            bridge = net.addSwitch("s1")
            self.assertFalse(bridge.batch)
            self.assertEqual(bridge.failMode, "standalone")
            net.built = True  # There are deliberately no kernel links or hosts to build.
            if fail_start:
                with self.assertRaisesRegex(RuntimeError, "timed out after committing"):
                    net.start()
            else:
                net.start()
            start_calls = list(ovs.call_args_list)
            self.assertEqual(len(start_calls), 1)
            self.assertEqual(start_calls[0].args[:2], ("add-br", "s1"))
            self.assertNotIn("del-br", start_calls[0].args)
            self.assertIn("external_ids:mqtt-quic-owner=this-run", start_calls[0].args)
            net.stop()
            stop_node.assert_called_once_with(bridge, deleteIntfs=True)
            self.assertTrue(bridge.terminate.called)
            if current_owner is None:
                self.assertEqual(stderr.getvalue(), "")
            return ovs.call_args_list

    def test_real_mininet_dispatch_starts_and_scopes_owned_cleanup(self):
        calls = self.run_lifecycle("this-run")
        self.assertEqual([call.args for call in calls if "del-br" in call.args], [("del-br", "s1")])

    def test_real_mininet_batch_shutdown_cannot_delete_foreign_bridge(self):
        calls = self.run_lifecycle("another-run")
        self.assertFalse(any("del-br" in call.args for call in calls))

    def test_partial_start_failure_cleans_only_own_committed_bridge(self):
        for current_owner in ("this-run", "another-run", None):
            with self.subTest(current_owner=current_owner):
                calls = self.run_lifecycle(current_owner, fail_start=True)
                deletions = [call.args for call in calls if "del-br" in call.args]
                self.assertEqual(deletions, [("del-br", "s1")] if current_owner == "this-run" else [])


if __name__ == "__main__":
    unittest.main()
