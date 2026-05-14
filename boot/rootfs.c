__attribute__((visibility("hidden"))) int OrlixSelectRootImage(const char *path) {
    if (!path || path[0] == '\0') {
        return -1;
    }
    return 0;
}
