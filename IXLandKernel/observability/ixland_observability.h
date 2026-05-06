#ifndef IXLAND_OBSERVABILITY_H
#define IXLAND_OBSERVABILITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IXLAND_OBSERVABILITY_VERSION 1

typedef enum {
    IXLAND_OBS_DEBUG = 0,
    IXLAND_OBS_INFO = 1,
    IXLAND_OBS_WARN = 2,
    IXLAND_OBS_ERROR = 3,
} ixland_observability_level_t;

int ixland_observability_init(void);
void ixland_observability_deinit(void);

void ixland_log(ixland_observability_level_t level, const char *fmt, ...);

#define IXLAND_LOG_DEBUG(...) ixland_log(IXLAND_OBS_DEBUG, __VA_ARGS__)
#define IXLAND_LOG_INFO(...) ixland_log(IXLAND_OBS_INFO, __VA_ARGS__)
#define IXLAND_LOG_WARN(...) ixland_log(IXLAND_OBS_WARN, __VA_ARGS__)
#define IXLAND_LOG_ERROR(...) ixland_log(IXLAND_OBS_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif