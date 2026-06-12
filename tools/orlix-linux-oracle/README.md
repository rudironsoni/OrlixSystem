# Orlix Linux Oracle

Mac-only tooling for comparing Orlix behavior against a real Linux reference.

This directory is not part of the iOS runtime. Nothing here may be linked into
`OrlixKernel`, `OrlixOS`, `OrlixHostAdapter`, or app targets. Apple container,
Apple containerization, and Virtualization.framework may only appear later as
external runner options for producing real-Linux result files.

## Contract

An oracle case declares:

- a stable case id;
- a fixture program or shell command;
- argv, env, and cwd;
- which outputs and observations must match.

Each runner produces a result JSON file. The comparator checks the real-Linux
result against the Orlix result and exits nonzero on drift.

The first scope is deterministic syscall and filesystem behavior:

- stdout and stderr;
- exit status or signal;
- errno observations;
- stat metadata;
- filesystem mutations;
- procfs and mount observations.

## Commands

Validate a case file:

```bash
swift tools/orlix-linux-oracle/orlix-linux-oracle.swift validate-case \
  tools/orlix-linux-oracle/cases/path-errno.json
```

Compare two captured result files:

```bash
swift tools/orlix-linux-oracle/orlix-linux-oracle.swift compare \
  --case tools/orlix-linux-oracle/cases/path-errno.json \
  --linux-result tools/orlix-linux-oracle/samples/path-errno.linux.json \
  --orlix-result tools/orlix-linux-oracle/samples/path-errno.orlix.json
```

Produce a Linux result from a prebuilt fixture binary inside a real Linux
environment:

```bash
swift tools/orlix-linux-oracle/orlix-linux-oracle.swift linux-result-from-fixture \
  --case tools/orlix-linux-oracle/cases/path-errno.json \
  --fixture /oracle/path_errno_probe \
  --workdir /oracle/work \
  --output /oracle/results/path-errno.linux.json
```

This command intentionally refuses to run on non-Linux hosts. On macOS, use it
only from an external Linux runner such as a developer VM, a Linux CI job, or a
future external Apple-container-based oracle runner. That runner remains
tooling only and must not become an iOS runtime dependency.

Convert an Orlix kselftest log with an oracle-marked block into a result file:

```bash
swift tools/orlix-linux-oracle/orlix-linux-oracle.swift orlix-result-from-log \
  --case tools/orlix-linux-oracle/cases/path-errno.json \
  --log tools/orlix-linux-oracle/samples/path-errno.orlix-kselftest.log \
  --output /tmp/path-errno.orlix.json
```

## Boundaries

Allowed:

- compile fixtures for Linux runners;
- run real Linux externally on a Mac development host;
- run Orlix through app-hosted proof drivers;
- compare captured files.

Forbidden:

- adding Apple container/containerization as an iOS app dependency;
- adding Virtualization.framework as an iOS runtime dependency;
- changing OrlixKernel Linux semantics from this tool;
- treating host path behavior as Linux truth;
- using this comparator result as proof unless both runner result files were
  produced by the declared case.
