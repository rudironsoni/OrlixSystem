include $(PROJECT_DIR)/Sources/make/sources.mk
include $(PROJECT_DIR)/Sources/make/toolchain.mk
include $(PROJECT_DIR)/Sources/make/linux-feature-packages.mk
include $(PROJECT_DIR)/Sources/make/packages.mk
include $(PROJECT_DIR)/Sources/make/coreutils-test.mk
include $(PROJECT_DIR)/Sources/make/proof-packages.mk
include $(PROJECT_DIR)/Sources/make/rootfs.mk

clean:
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BUILD_ROOT)/build" "$(ORLIXOS_BUILD_ROOT)/packages" "$(ORLIXOS_BUILD_ROOT)/rootfs" "$(ORLIXOS_SRC_DIR)"

mrproper: clean
	@set -euo pipefail; \
	rm -rf "$(ORLIXOS_BUILD_ROOT)"
