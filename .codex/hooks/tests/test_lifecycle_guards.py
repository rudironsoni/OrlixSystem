#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


HOOK_DIR = Path(__file__).resolve().parents[1]
PRE_TOOL_GUARD = HOOK_DIR / "pre_tool_use_guard.py"
POST_TOOL_REVIEW = HOOK_DIR / "post_tool_use_review.py"
STOP_CLAIM_CHECK = HOOK_DIR / "stop_claim_check.py"
COMPACT_PLAN_CHECK = HOOK_DIR / "compact_plan_check.py"


def run_hook(script, payload, cwd=None, env=None):
    return subprocess.run(
        [sys.executable, str(script)],
        input=json.dumps(payload),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=cwd,
        env=env,
        check=False,
    )


def hook_env(state_dir):
    env = dict(os.environ)
    env["ORLIX_PLAN_GUARD_STATE_DIR"] = str(state_dir)
    return env


def bash_payload(command, event="PreToolUse"):
    return {
        "hook_event_name": event,
        "tool_name": "Bash",
        "tool_input": {"command": command},
    }


def write_payload(path, event="PreToolUse"):
    return {
        "hook_event_name": event,
        "tool_name": "apply_patch",
        "tool_input": {
            "patch": f"*** Begin Patch\n*** Update File: {path}\n@@\n-old\n+new\n*** End Patch\n"
        },
    }


def create_active_plan(root):
    plan = root / "docs" / "plans" / "active" / "demo"
    plan.mkdir(parents=True)
    (root / "AGENTS.md").write_text("# AGENTS.md\n")
    (plan / "GOAL.md").write_text("# Goal\n")
    (plan / "PLAN.md").write_text("# Plan\n")
    (plan / "IMPLEMENT.md").write_text("# IMPLEMENT.md\n")
    return plan


class LifecycleGuardTests(unittest.TestCase):
    def test_plan_context_guard_blocks_patch_before_plan_read(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as state_tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            create_active_plan(root)

            result = run_hook(
                PRE_TOOL_GUARD,
                write_payload("README.md"),
                cwd=root,
                env=hook_env(state_tmp),
            )

        self.assertEqual(result.returncode, 2)
        self.assertIn("Active plan context must be read before mutation", result.stderr)

    def test_plan_context_guard_blocks_mutating_rtk_before_plan_read(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as state_tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            create_active_plan(root)

            result = run_hook(
                PRE_TOOL_GUARD,
                bash_payload("rtk touch changed.txt"),
                cwd=root,
                env=hook_env(state_tmp),
            )

        self.assertEqual(result.returncode, 2)
        self.assertIn("Active plan context must be read before mutation", result.stderr)

    def test_plan_context_guard_allows_read_only_command_before_plan_read(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as state_tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            create_active_plan(root)

            result = run_hook(
                PRE_TOOL_GUARD,
                bash_payload("rtk rg -n Goal docs/plans/active/demo/GOAL.md"),
                cwd=root,
                env=hook_env(state_tmp),
            )

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_plan_context_guard_marks_context_loaded_after_required_reads(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as state_tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            create_active_plan(root)
            env = hook_env(state_tmp)

            read_result = run_hook(
                POST_TOOL_REVIEW,
                bash_payload(
                    "rtk sed -n '1,40p' AGENTS.md "
                    "docs/plans/active/demo/GOAL.md "
                    "docs/plans/active/demo/PLAN.md "
                    "docs/plans/active/demo/IMPLEMENT.md",
                    event="PostToolUse",
                ),
                cwd=root,
                env=env,
            )
            mutate_result = run_hook(
                PRE_TOOL_GUARD,
                write_payload("README.md"),
                cwd=root,
                env=env,
            )

        self.assertEqual(read_result.returncode, 0, read_result.stderr)
        self.assertEqual(mutate_result.returncode, 0, mutate_result.stderr)

    def test_plan_context_guard_blocks_commit_after_mutation_without_implement_update(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as state_tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            create_active_plan(root)
            env = hook_env(state_tmp)
            run_hook(
                POST_TOOL_REVIEW,
                bash_payload(
                    "rtk sed -n '1,40p' AGENTS.md "
                    "docs/plans/active/demo/GOAL.md "
                    "docs/plans/active/demo/PLAN.md "
                    "docs/plans/active/demo/IMPLEMENT.md",
                    event="PostToolUse",
                ),
                cwd=root,
                env=env,
            )
            run_hook(POST_TOOL_REVIEW, write_payload("README.md", event="PostToolUse"), cwd=root, env=env)

            result = run_hook(PRE_TOOL_GUARD, bash_payload("rtk git commit -m checkpoint"), cwd=root, env=env)

        self.assertEqual(result.returncode, 2)
        self.assertIn("IMPLEMENT.md must be updated after mutation", result.stderr)

    def test_plan_context_guard_allows_commit_after_implement_update(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as state_tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            create_active_plan(root)
            env = hook_env(state_tmp)
            run_hook(
                POST_TOOL_REVIEW,
                bash_payload(
                    "rtk sed -n '1,40p' AGENTS.md "
                    "docs/plans/active/demo/GOAL.md "
                    "docs/plans/active/demo/PLAN.md "
                    "docs/plans/active/demo/IMPLEMENT.md",
                    event="PostToolUse",
                ),
                cwd=root,
                env=env,
            )
            run_hook(POST_TOOL_REVIEW, write_payload("README.md", event="PostToolUse"), cwd=root, env=env)
            run_hook(
                POST_TOOL_REVIEW,
                write_payload("docs/plans/active/demo/IMPLEMENT.md", event="PostToolUse"),
                cwd=root,
                env=env,
            )

            result = run_hook(PRE_TOOL_GUARD, bash_payload("rtk git commit -m checkpoint"), cwd=root, env=env)

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_plan_context_guard_allows_push_after_commit_gate_passes(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as state_tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            create_active_plan(root)
            env = hook_env(state_tmp)
            run_hook(
                POST_TOOL_REVIEW,
                bash_payload(
                    "rtk sed -n '1,40p' AGENTS.md "
                    "docs/plans/active/demo/GOAL.md "
                    "docs/plans/active/demo/PLAN.md "
                    "docs/plans/active/demo/IMPLEMENT.md",
                    event="PostToolUse",
                ),
                cwd=root,
                env=env,
            )
            run_hook(POST_TOOL_REVIEW, write_payload("README.md", event="PostToolUse"), cwd=root, env=env)
            run_hook(
                POST_TOOL_REVIEW,
                write_payload("docs/plans/active/demo/IMPLEMENT.md", event="PostToolUse"),
                cwd=root,
                env=env,
            )
            commit_pre = run_hook(PRE_TOOL_GUARD, bash_payload("rtk git commit -m checkpoint"), cwd=root, env=env)
            commit_post = run_hook(
                POST_TOOL_REVIEW,
                bash_payload("rtk git commit -m checkpoint", event="PostToolUse"),
                cwd=root,
                env=env,
            )
            push_pre = run_hook(PRE_TOOL_GUARD, bash_payload("rtk git push origin main"), cwd=root, env=env)

        self.assertEqual(commit_pre.returncode, 0, commit_pre.stderr)
        self.assertEqual(commit_post.returncode, 0, commit_post.stderr)
        self.assertEqual(push_pre.returncode, 0, push_pre.stderr)

    def test_stop_claim_check_warns_on_oci_runtime_claim_without_lifecycle(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "OCI runtime support is complete."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("OCI Runtime support claim detected", result.stderr)

    def test_stop_claim_check_warns_on_orlix_run_without_lifecycle(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "`orlix run` works now."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("`orlix run` completion claim detected", result.stderr)

    def test_stop_claim_check_warns_on_oci_compatible_from_image_only(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "OCI compatible after OCI image import proof."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("OCI compatibility claim appears based only on image/import proof", result.stderr)

    def test_stop_claim_check_warns_on_stale_remaining_task_list(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Continue the stale 14-item remaining list."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("Stale remaining-task list wording detected", result.stderr)

    def test_post_tool_review_warns_on_failure_output(self):
        result = run_hook(
            POST_TOOL_REVIEW,
            {
                "hook_event_name": "PostToolUse",
                "tool_name": "Bash",
                "tool_response": "ORLIX-COREUTILS-TEST-END failures=1 skips=0",
            },
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("ORLIX-HARNESS-WARN", result.stderr)

    def test_stop_claim_check_warns_without_active_plan(self):
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(["git", "init"], cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)

            result = run_hook(
                STOP_CLAIM_CHECK,
                {"hook_event_name": "Stop", "response": "This is complete."},
                cwd=tmp,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("no active plan directory exists", result.stderr)

    def test_stop_claim_check_warns_when_active_evidence_is_not_current_claim_relevant(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            plan = root / "docs" / "plans" / "active" / "demo"
            plan.mkdir(parents=True)
            (plan / "IMPLEMENT.md").write_text(
                "\n".join(
                    [
                        "# IMPLEMENT.md",
                        "",
                        "**Evidence:**",
                        "",
                        "- `rtk echo unrelated` printed `ok`.",
                    ]
                )
            )

            result = run_hook(
                STOP_CLAIM_CHECK,
                {
                    "hook_event_name": "Stop",
                    "response": "The package-ready proof is complete.",
                },
                cwd=root,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("do not contain claim-relevant evidence", result.stderr)

    def test_stop_claim_check_warns_on_macos_runtime_wording(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "This is aligned with macOS runtime behavior."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("macOS runtime/user wording detected", result.stderr)

    def test_stop_claim_check_allows_negated_macos_runtime_wording(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "No macOS runtime target was added."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("macOS runtime/user wording detected", result.stderr)

    def test_stop_claim_check_warns_on_generic_container_support(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "The plan adds Docker support."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("Generic Docker/container support wording detected", result.stderr)

    def test_stop_claim_check_allows_negated_container_support(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Do not add Docker support."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("Generic Docker/container support wording detected", result.stderr)

    def test_stop_claim_check_warns_on_unscoped_named_mechanism(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Next we create OrlixImageBuilder."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("named tool, workflow root, or mechanism-specific proof target", result.stderr)

    def test_stop_claim_check_allows_negated_named_mechanism(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Do not create OrlixImageBuilder."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("named tool, workflow root, or mechanism-specific proof target", result.stderr)

    def test_stop_claim_check_warns_on_unauthorized_workflow_delegation(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Split this into subagents and a parallel workflow."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("without explicit user authorization", result.stderr)

    def test_stop_claim_check_allows_negated_workflow_delegation(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Do not spawn subagents without explicit user authorization."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("without explicit user authorization", result.stderr)

    def test_compact_plan_check_warns_on_missing_implementation_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            plan = root / "docs" / "plans" / "active" / "demo"
            plan.mkdir(parents=True)
            (plan / "PLAN.md").write_text("# plan\n")

            result = subprocess.run(
                [sys.executable, str(COMPACT_PLAN_CHECK)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=root,
                check=False,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("missing IMPLEMENT.md", result.stderr)

    def test_compact_plan_check_warns_on_stale_status_contradiction(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            plan = root / "docs" / "plans" / "active" / "demo"
            plan.mkdir(parents=True)
            (plan / "PLAN.md").write_text("# plan\n")
            (plan / "IMPLEMENT.md").write_text(
                "\n".join(
                    [
                        "# IMPLEMENT.md",
                        "",
                        "## earlier",
                        "",
                        "Current status:",
                        "",
                        "- pending on simulator proof.",
                        "",
                        "**Evidence:**",
                        "",
                        "- `rtk echo first`",
                        "",
                        "## later",
                        "",
                        "Current status:",
                        "",
                        "- green after iOS Simulator proof succeeded.",
                        "",
                        "**Evidence:**",
                        "",
                        "- `rtk xcodebuild test` succeeded.",
                    ]
                )
            )

            result = subprocess.run(
                [sys.executable, str(COMPACT_PLAN_CHECK)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=root,
                check=False,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("stale pending/blocked status", result.stderr)

    def test_compact_plan_check_warns_without_recent_handoff_or_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            plan = root / "docs" / "plans" / "active" / "demo"
            plan.mkdir(parents=True)
            (plan / "PLAN.md").write_text("# plan\n")
            (plan / "IMPLEMENT.md").write_text(
                "\n".join(
                    [
                        "# IMPLEMENT.md",
                        "",
                        "**Evidence:**",
                        "",
                        "- `rtk echo old` recorded old evidence.",
                    ]
                )
            )

            result = subprocess.run(
                [sys.executable, str(COMPACT_PLAN_CHECK)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=root,
                check=False,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("no recent current-status or handoff marker", result.stderr)


if __name__ == "__main__":
    unittest.main()
