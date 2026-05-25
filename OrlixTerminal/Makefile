SHELL := /bin/bash
.DEFAULT_GOAL := all

PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(PROJECT_DIR)/..)
KERNEL_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixKernel/Makefile
ORLIXOS_MAKE := $(MAKE) -C $(REPO_ROOT) -f OrlixOS/Makefile

PROFILE ?= appstore
XCODEBUILD_MCP ?= xcodebuildmcp
ORLIX_TERMINAL_SHELL_PROOF ?= 1
ORLIX_TERMINAL_RUNTIME_LOG_WAIT_TICKS ?= 200
ORLIX_TERMINAL_SHELL_PROOF_WAIT_TICKS ?= 400
ORLIX_TERMINAL_RUN_DIR := $(REPO_ROOT)/Build/OrlixTerminal/run/$(PROFILE)
ORLIX_TERMINAL_RUN_LOG := $(ORLIX_TERMINAL_RUN_DIR)/orlix-terminal-run.log
ORLIX_KERNEL_BASE_ROOT_TREE_INPUT ?= $(REPO_ROOT)/Build/OrlixOS/rootfs/$(PROFILE)/base-tree

.PHONY: all help build run clean mrproper

all: build

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' '  build     validate OrlixTerminal source ownership'
	@printf '%s\n' '  run       build and run the OrlixTerminal iOS host app'
	@printf '%s\n' '  clean     no project-local generated outputs'

build:
	@printf '%s\n' 'OrlixTerminal is built through the generated OrlixTerminal Xcode scheme.'

run:
	@$(ORLIXOS_MAKE) rootfs PROFILE="$(PROFILE)"
	@set -euo pipefail; \
	command -v "$(XCODEBUILD_MCP)" >/dev/null 2>&1 || { echo "XcodeBuildMCP is required; install xcodebuildmcp or set XCODEBUILD_MCP=/path/to/xcodebuildmcp" >&2; exit 1; }; \
	mkdir -p "$(ORLIX_TERMINAL_RUN_DIR)"; \
	run_log="$(ORLIX_TERMINAL_RUN_LOG)"; \
	rm -f "$$run_log"; \
	$(KERNEL_MAKE) run PROFILE="$(PROFILE)" ORLIX_KERNEL_BASE_ROOT_TREE_INPUT="$(ORLIX_KERNEL_BASE_ROOT_TREE_INPUT)" | tee "$$run_log"; \
	runtime_log="$$(sed -n 's/.*"runtimeLogPath": "\([^"]*\)".*/\1/p' "$$run_log" | tail -n 1)"; \
	simulator_id="$$(sed -n 's/.*"simulatorId": "\([^"]*\)".*/\1/p' "$$run_log" | tail -n 1)"; \
	[ -n "$$runtime_log" ] || { echo "missing OrlixTerminal runtime log path in $$run_log" >&2; exit 1; }; \
	[ -n "$$simulator_id" ] || { echo "missing simulator id in $$run_log" >&2; exit 1; }; \
	for _ in $$(seq 1 $(ORLIX_TERMINAL_RUNTIME_LOG_WAIT_TICKS)); do [ -s "$$runtime_log" ] && break; sleep 0.1; done; \
	[ -s "$$runtime_log" ] || { echo "missing OrlixTerminal runtime log: $$runtime_log" >&2; exit 1; }; \
	if grep -E 'Kernel panic|hosted user trap|Darwin syscall' "$$runtime_log"; then \
		echo "OrlixTerminal runtime reported kernel failure: $$runtime_log" >&2; \
		exit 1; \
	fi; \
	if [ "$(ORLIX_TERMINAL_SHELL_PROOF)" = "1" ]; then \
		for _ in $$(seq 1 $(ORLIX_TERMINAL_SHELL_PROOF_WAIT_TICKS)); do \
			grep -F -q 'sh-5.3# ' "$$runtime_log" && break; \
			if grep -E -q 'Kernel panic|hosted user trap|Darwin syscall' "$$runtime_log"; then break; fi; \
			sleep 0.1; \
		done; \
		grep -F -q 'sh-5.3# ' "$$runtime_log" || { echo "Bash prompt did not appear in OrlixTerminal runtime log: $$runtime_log" >&2; exit 1; }; \
		proof_text=$$'echo ORLIX_SHELL_ECHO_OK; pwd; cd /tmp && pwd && echo ORLIX_CD_TMP_OK; echo ORLIX_REDIRECT_OK > /tmp/orlix-proof.txt; read ORLIX_REDIRECT_VALUE < /tmp/orlix-proof.txt; echo $$ORLIX_REDIRECT_VALUE; printf "ORLIX_PIPE_OK\\n" | while read line; do echo $$line; done; /bin/sh -c "exit 7"; echo ORLIX_EXIT_STATUS_$$?; trap "echo ORLIX_SIGNAL_OK" TERM; kill -TERM $$$$; echo ORLIX_SHELL_PROOF_END\n'; \
		"$(XCODEBUILD_MCP)" ui-automation type-text --simulator-id "$$simulator_id" --text "$$proof_text" --output json >/dev/null; \
		for _ in $$(seq 1 $(ORLIX_TERMINAL_SHELL_PROOF_WAIT_TICKS)); do \
			if grep -E -q 'Kernel panic|hosted user trap|Darwin syscall|Read-only file system' "$$runtime_log"; then break; fi; \
			grep -F -q 'ORLIX_SHELL_PROOF_END' "$$runtime_log" && break; \
			sleep 0.1; \
		done; \
		if grep -E 'Kernel panic|hosted user trap|Darwin syscall|Read-only file system' "$$runtime_log"; then \
			echo "OrlixTerminal shell proof reported failure: $$runtime_log" >&2; \
			exit 1; \
		fi; \
		for marker in ORLIX_SHELL_ECHO_OK ORLIX_CD_TMP_OK ORLIX_REDIRECT_OK ORLIX_PIPE_OK ORLIX_EXIT_STATUS_7 ORLIX_SIGNAL_OK ORLIX_SHELL_PROOF_END; do \
			grep -F -q "$$marker" "$$runtime_log" || { echo "missing Bash proof marker $$marker in $$runtime_log" >&2; exit 1; }; \
		done; \
		echo "verified OrlixTerminal Bash proof in simulator log: $$runtime_log"; \
	fi

clean:
	@true

mrproper: clean
