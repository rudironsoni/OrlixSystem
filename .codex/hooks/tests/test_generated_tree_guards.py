#!/usr/bin/env python3
import json
import subprocess
import sys
import unittest
from pathlib import Path


HOOK_DIR = Path(__file__).resolve().parents[1]
PRE_TOOL_GUARD = HOOK_DIR / "pre_tool_use_guard.py"
PERMISSION_GUARD = HOOK_DIR / "permission_request_guard.py"


def run_hook(script, payload):
    return subprocess.run(
        [sys.executable, str(script)],
        input=json.dumps(payload),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def bash_payload(command):
    return {
        "hook_event_name": "PreToolUse",
        "tool_name": "Bash",
        "tool_input": {"command": command},
    }


class GeneratedTreeGuardTests(unittest.TestCase):
    def test_pre_tool_guard_allows_read_only_bash_generated_tree_inspection(self):
        result = run_hook(
            PRE_TOOL_GUARD,
            bash_payload(
                "rtk rg -n 'SYSCALL_DEFINE' "
                "Build/OrlixKernel/src/linux-6.12-port/kernel/fork.c"
            ),
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("ORLIX-HARNESS-BLOCK", result.stderr)

    def test_pre_tool_guard_blocks_patch_edits_to_generated_trees(self):
        result = run_hook(
            PRE_TOOL_GUARD,
            {
                "hook_event_name": "PreToolUse",
                "tool_name": "apply_patch",
                "tool_input": {
                    "patch": "\n".join(
                        [
                            "*** Begin Patch",
                            "*** Update File: Build/OrlixMLibC/src/mlibc-1.0/sysdeps/orlix/foo.cpp",
                            "@@",
                            "-old",
                            "+new",
                            "*** End Patch",
                        ]
                    )
                },
            },
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("ORLIX-HARNESS-BLOCK", result.stderr)

    def test_pre_tool_guard_blocks_bash_mutation_to_generated_trees(self):
        result = run_hook(
            PRE_TOOL_GUARD,
            bash_payload(
                "rtk sed -i '' 's/old/new/' "
                "Build/OrlixOS/upstream/coreutils-9.5.git/src/cat.c"
            ),
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("ORLIX-HARNESS-BLOCK", result.stderr)

    def test_pre_tool_guard_blocks_wrapped_builds_in_generated_trees(self):
        result = run_hook(
            PRE_TOOL_GUARD,
            bash_payload(
                "rtk timeout 1200 make -C "
                "Build/OrlixKernel/src/linux-6.12-port oldconfig"
            ),
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("ORLIX-HARNESS-BLOCK", result.stderr)

    def test_pre_tool_guard_blocks_script_writes_to_generated_trees(self):
        result = run_hook(
            PRE_TOOL_GUARD,
            bash_payload(
                "rtk python3 - <<'PY'\n"
                "from pathlib import Path\n"
                "Path('Build/OrlixMLibC/src/mlibc-1.0/foo.cpp').write_text('x')\n"
                "PY"
            ),
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("ORLIX-HARNESS-BLOCK", result.stderr)

    def test_permission_guard_allows_read_only_generated_tree_bash_request(self):
        result = run_hook(
            PERMISSION_GUARD,
            {
                "hook_event_name": "PermissionRequest",
                "tool_name": "Bash",
                "tool_input": {
                    "command": "rtk sed -n '1,80p' "
                    "Build/OrlixMLibC/upstream/mlibc-1.0.git/options/posix/generic/unistd.cpp"
                },
            },
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("ORLIX-HARNESS-BLOCK", result.stderr)


if __name__ == "__main__":
    unittest.main()
