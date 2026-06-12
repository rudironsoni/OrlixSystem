---
name: orlix-native-performance
description: Use when discussing, planning, measuring, or claiming Orlix native performance, ELF execution performance, syscall throughput, terminal throughput, storage throughput, or imported-binary startup behavior.
---

# Orlix Native Performance

Performance claims need workload evidence, not architecture intent.

## Required Evidence

- Exact binary or workload.
- Exact simulator or device target.
- Build configuration.
- Trigger command or user action.
- Baseline or previous run.
- Measurement command or profiler capture.
- Hotspot, latency, throughput, or allocation result.
- What was not measured.

## Initial Orlix Focus

- ELF launch latency.
- `execve` and dynamic-loader path cost.
- Syscall throughput for hot paths.
- PTY read/write throughput and readiness latency.
- Rootfs tar and OCI layout import time.
- Virtio-blk persistence and I/O throughput.
- Virtio-fs path lookup and file operation latency.
- Imported-binary startup latency in iOS Simulator.

## Rules

- Do not claim native performance from “compiled successfully.”
- Do not claim full performance from one microbenchmark.
- Do not compare against macOS runtime behavior for Orlix’s initial runtime target.
- Label Simulator-only results as Simulator-only.
