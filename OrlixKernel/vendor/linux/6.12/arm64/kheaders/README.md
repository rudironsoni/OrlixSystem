# Vendored Linux Headers

This tuple was generated from upstream Linux sources.
Do not edit vendored files manually.

Regenerate with:

```sh
make vendor-orlixkernel-linux-headers LINUX_VERSION=<version> LINUX_ARCH=<arch>
```

Surface: include

- `include`: flattened Linux kernel header root for OrlixKernel, including the upstream `uapi/` subtree required by the full Linux header graph; generated staging namespaces are excluded.
