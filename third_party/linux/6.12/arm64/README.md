# Vendored Linux Headers

This tuple was generated from upstream Linux sources.
Do not edit vendored files manually.

Regenerate with:

```sh
make vendor-linux-headers LINUX_VERSION=<version> LINUX_ARCH=<arch>
```

Surfaces:

- `uapi/include`: output from `make headers_install`.
- `srctree`: copied from extracted Linux source include roots.
- `objtree`: copied from generated Linux build include roots (O=<objtree>).
