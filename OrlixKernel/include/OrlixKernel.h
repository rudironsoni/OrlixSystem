#ifndef ORLIX_KERNEL_H
#define ORLIX_KERNEL_H

#ifdef __cplusplus
extern "C" {
#endif

struct boot_params {
    const void *kernel_image;
    unsigned long kernel_image_size;
    const void *initrd_image;
    unsigned long initrd_size;
    const char *root_image_path;
    const void *device_tree;
    unsigned long device_tree_size;
    unsigned long flags;
};

__attribute__((visibility("default"))) int OrlixPrepareBootParams(struct boot_params *params);
__attribute__((visibility("default"))) int OrlixBoot(const struct boot_params *params);

#ifdef __cplusplus
}
#endif

#endif
