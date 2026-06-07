include $(PROJECT_DIR)/Sources/make/sources.mk
include $(PROJECT_DIR)/Sources/make/toolchain.mk
include $(PROJECT_DIR)/Sources/make/linux-feature-packages.mk
include $(PROJECT_DIR)/Sources/make/packages.mk
include $(PROJECT_DIR)/Sources/make/coreutils-test.mk
include $(PROJECT_DIR)/Sources/make/proof-packages.mk
include $(PROJECT_DIR)/Sources/make/rootfs.mk

clean:
	@set -euo pipefail; \
	if [ -L "$(ORLIXOS_BUILD_ROOT)" ]; then echo "refusing to clean symlinked OrlixOS build root: $(ORLIXOS_BUILD_ROOT)" >&2; exit 1; fi; \
	rm -rf "$(ORLIXOS_BUILD_ROOT)"

mrproper: clean
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BUILD_ROOT)"
