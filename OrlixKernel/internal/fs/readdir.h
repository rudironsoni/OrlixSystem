#ifndef INTERNAL_FS_READDIR_H
#define INTERNAL_FS_READDIR_H

#define BACKING_DIR_NAME_MAX 256

struct backing_dir_stream;

struct backing_dir_record {
    unsigned long long ino;
    long long off;
    unsigned char type;
    char name[BACKING_DIR_NAME_MAX];
};

int backing_dir_open(int fd, long long offset, struct backing_dir_stream **out_stream);
int backing_dir_read(struct backing_dir_stream *stream, struct backing_dir_record *record);
void backing_dir_close(struct backing_dir_stream *stream);

#endif
