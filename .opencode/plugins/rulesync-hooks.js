export const RulesyncHooksPlugin = async ({ $ }) => {
  return {
    event: async ({ event }) => {
      if (event.type === "permission.asked") {
        await $`/usr/bin/python3 "$(git rev-parse --show-toplevel)/.codex/hooks/permission_request_guard.py"`;
      }
      else if (event.type === "session.idle") {
        await $`/usr/bin/python3 "$(git rev-parse --show-toplevel)/.codex/hooks/stop_claim_check.py"`;
      }
    },
    "tool.execute.before": async (input) => {
      {
        const __re = new RegExp("Bash|apply_patch|Edit|Write");
        if (__re.test(input.tool)) {
          await $`/usr/bin/python3 "$(git rev-parse --show-toplevel)/.codex/hooks/pre_tool_use_guard.py"`;
        }
      }
    },
    "tool.execute.after": async (input) => {
      {
        const __re = new RegExp("Bash|apply_patch|Edit|Write");
        if (__re.test(input.tool)) {
          await $`/usr/bin/python3 "$(git rev-parse --show-toplevel)/.codex/hooks/post_tool_use_review.py"`;
        }
      }
    },
  };
};
