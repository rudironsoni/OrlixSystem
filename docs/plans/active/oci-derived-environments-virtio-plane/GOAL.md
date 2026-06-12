# Goal

Design and execute the implementation path for Orlix to run OCI/container-image-derived Linux environments alongside the default Orlix Linux environment while preserving Orlix's Linux-shaped architecture.

Success means Orlix keeps one OrlixKernel as the Linux compatibility surface, uses upstream Linux behavior and UAPI as contract truth, keeps OrlixHostAdapter limited to private host mediation, keeps MLibC in userspace, and forbids Darwin, Foundation, POSIX host headers, Apple container/containerization, Virtualization.framework, VM lifecycle, vminitd, vmnet, Rosetta, runc, Docker daemon, and local Linux ABI clones from OrlixKernel and the iOS runtime.

The current goal includes OCI Runtime Spec alignment. OrlixOS translates an OCI runtime bundle and `config.json` into Orlix environment descriptors, mount policy, process configuration, and lifecycle state. Orlix does not become `runc`, depend on `runc`, or expose a Docker daemon path.

The plan must stage proof-driven work for named environments, import-to-enter flow, OCI image layout import, OCI Runtime config/schema/lifecycle behavior, truthful OCI feature reporting, host-folder mounts, virtio-fs, Linux oracle expansion, native performance, `orlix run`, registry pull tooling, networking, namespaces, and virtual cgroups. Each phase must name repo changes, likely files or areas, tests, proof criteria, risks, blockers, and explicit non-actions.

The intended product model is one OrlixKernel with multiple Linux environments: the default Orlix root, imported rootfs environments, and OCI-derived environments. OCI image metadata and OCI runtime config may configure an Orlix environment, but Linux semantics remain owned by OrlixKernel. Virtio is an internal device plane below Linux subsystems, not a public userspace ABI and not a VM runtime.
