# Vendored Linux Headers

IXLandKernel vendors three distinct Linux-shaped header surfaces under `third_party/linux/<version>/<arch>/`:

- `uapi/include`: exported userspace headers from Linux `headers_install`.
- `kheaders/source`: copied non-generated Linux kernel source headers.
- `kheaders/generated`: copied generated Linux kernel build headers from the Linux O= output.

Regenerate the vendored tree with:

```sh
make vendor-linux-headers LINUX_VERSION=<version> LINUX_ARCH=<arch>
```

Do not hand-edit vendored Linux headers. Regenerate them from pristine upstream Linux sources through the Makefile pipeline.
