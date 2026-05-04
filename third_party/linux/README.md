# Vendored Linux Headers

IXLandSystem vendors three distinct Linux-shaped header surfaces under `third_party/linux/<version>/<arch>/`:

- `uapi/include`: exported userspace headers from Linux `headers_install`.
- `srctree`: Linux source include roots copied from the extracted Linux tarball.
- `objtree`: Linux build include roots copied from the generated O=<objtree> output.

Regenerate the vendored tree with:

```sh
make vendor-linux-headers LINUX_VERSION=<version> LINUX_ARCH=<arch>
```

Do not hand-edit vendored Linux headers. Regenerate them from pristine upstream Linux sources through the Makefile pipeline.
