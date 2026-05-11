#ifndef ORLIX_OBSERVABILITY_H
#define ORLIX_OBSERVABILITY_H

#ifdef __cplusplus
extern "C" {
#endif

#define ORLIX_OBSERVABILITY_VERSION 1

typedef enum {
    ORLIX_OBS_DEBUG = 0,
    ORLIX_OBS_INFO = 1,
    ORLIX_OBS_WARN = 2,
    ORLIX_OBS_ERROR = 3,
} orlix_observability_level_t;

int orlix_observability_init(void);
void orlix_observability_deinit(void);

void orlix_log(orlix_observability_level_t level, const char *fmt, ...);

#define ORLIX_LOG_DEBUG(...) orlix_log(ORLIX_OBS_DEBUG, __VA_ARGS__)
#define ORLIX_LOG_INFO(...) orlix_log(ORLIX_OBS_INFO, __VA_ARGS__)
#define ORLIX_LOG_WARN(...) orlix_log(ORLIX_OBS_WARN, __VA_ARGS__)
#define ORLIX_LOG_ERROR(...) orlix_log(ORLIX_OBS_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
