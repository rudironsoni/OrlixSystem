__attribute__((visibility("hidden"))) int OrlixSelectRootImage(const char *device) {
    if (!device || device[0] == '\0') {
        return -1;
    }
    return 0;
}
