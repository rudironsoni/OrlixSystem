# Linux Patch Exceptions

Patch files under `OrlixKernel/Sources/ports/orlix/patches` must not modify upstream Linux owner paths unless the patch has an explicit exception file in this directory.

Forbidden upstream paths:

- `fs/`
- `kernel/`
- `mm/`
- `ipc/`
- `net/`
- `include/linux/`
- `include/uapi/`

For a patch named `example.patch`, add `OrlixKernel/Sources/ports/orlix/patches/exceptions/example.patch.md` before running `make prepare`. The exception must explain why the upstream-path change is required and why it cannot live in the Orlix overlay.
