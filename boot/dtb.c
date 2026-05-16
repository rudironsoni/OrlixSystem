__attribute__((visibility("hidden"))) int OrlixLoadDeviceTree(const void *image,
                                                              unsigned long image_size) {
    if (!image || image_size == 0) {
        return -1;
    }
    return 0;
}
