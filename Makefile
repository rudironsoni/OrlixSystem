LINUX_VERSION ?=
LINUX_ARCH ?=
LINUX_KERNEL_SERIES ?=
LINUX_TARBALL_URL ?=
LINUX_VENDOR_ROOT ?= third_party/linux
KEEP_LINUX_TMP ?= 0

.PHONY: vendor-linux-headers
vendor-linux-headers:
	@if [ -z "$(LINUX_VERSION)" ] || [ -z "$(LINUX_ARCH)" ]; then \
		echo "usage: make vendor-linux-headers LINUX_VERSION=<version> LINUX_ARCH=<arch>" >&2; \
		exit 2; \
	fi
	LINUX_VERSION="$(LINUX_VERSION)" \
	LINUX_ARCH="$(LINUX_ARCH)" \
	LINUX_KERNEL_SERIES="$(LINUX_KERNEL_SERIES)" \
	LINUX_TARBALL_URL="$(LINUX_TARBALL_URL)" \
	LINUX_VENDOR_ROOT="$(LINUX_VENDOR_ROOT)" \
	KEEP_LINUX_TMP="$(KEEP_LINUX_TMP)" \
	bash ./scripts/vendor_linux_headers.sh
