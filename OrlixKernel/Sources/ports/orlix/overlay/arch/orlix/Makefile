KBUILD_DEFCONFIG := defconfig

core-y += arch/orlix/kernel/
core-y += arch/orlix/mm/
core-y += arch/orlix/boot/

# Orlix product proof is app-hosted. A manual `make ... ARCH=orlix vmlinux`
# invocation is retained only as an explicit Linux Kbuild compatibility
# experiment; the default Orlix Kbuild path prepares generated state only.
all: prepare
