#ifndef ORLIX_KERNEL_TESTS_SUITE_REGISTRY_H
#define ORLIX_KERNEL_TESTS_SUITE_REGISTRY_H

#include "kunit.h"

const struct kunit_suite *kernel_process_group_session_suite(void);
const struct kunit_suite *kernel_task_group_suite(void);
const struct kunit_suite *kernel_signal_suite(void);
const struct kunit_suite *kernel_wait_suite(void);
const struct kunit_suite *kernel_futex_suite(void);
const struct kunit_suite *fs_pty_job_control_suite(void);
const struct kunit_suite *fs_pipe_suite(void);
const struct kunit_suite *fs_vfs_path_suite(void);
const struct kunit_suite *fs_rootfs_bootstrap_suite(void);

#endif
