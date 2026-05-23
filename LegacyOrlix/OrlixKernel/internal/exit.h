#ifndef INTERNAL_EXIT_H
#define INTERNAL_EXIT_H

#ifdef __cplusplus
extern "C" {
#endif

void process_terminate(int status) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
