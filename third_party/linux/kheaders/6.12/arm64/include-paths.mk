LINUX_KHEADERS_ROOT := third_party/linux/kheaders/6.12/arm64
LINUX_KHEADERS_CPPFLAGS := \
  -I$(LINUX_KHEADERS_ROOT)/objtree/arch/arm64/include/generated \
  -I$(LINUX_KHEADERS_ROOT)/srctree/arch/arm64/include \
  -I$(LINUX_KHEADERS_ROOT)/objtree/include \
  -I$(LINUX_KHEADERS_ROOT)/srctree/include
