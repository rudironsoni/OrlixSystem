# Goal

Design the implementation path for Orlix to run OCI/container-image-derived Linux environments alongside the default Orlix Linux environment while preserving Orlix's Linux-shaped architecture.

Success means the plan is specific to the inspected OrlixSystem repository and can be executed by a later coding agent without reinterpreting ownership boundaries. It must keep one OrlixKernel as the Linux compatibility surface, use upstream Linux behavior and UAPI as contract truth, keep OrlixHostAdapter limited to private host mediation, keep MLibC in userspace, and forbid Darwin, Foundation, POSIX host headers, Apple container/containerization, Virtualization.framework, VM lifecycle, vminitd, vmnet, Rosetta, runc, Docker daemon, and local Linux ABI clones from OrlixKernel and the iOS runtime.

The plan must stage the work in proof-driven phases: storage policy correction, Linux mount-backed roots, named environments, rootfs tar import, virtio core and initial devices, OCI layout import, overlay/snapshot behavior, registry tooling outside OrlixKernel, `orlix run`, networking, namespaces, and virtual cgroups. Each phase must name required repo changes, likely files or areas, tests, build commands, proof criteria, risks, blockers, and explicit non-actions.

The intended product model is one OrlixKernel with multiple Linux environments: the default Orlix root, imported rootfs environments, and OCI-derived environments. OCI image metadata may configure an Orlix environment, but Linux semantics remain owned by OrlixKernel. Virtio is an internal device plane below Linux subsystems, not a public userspace ABI and not a VM runtime.
