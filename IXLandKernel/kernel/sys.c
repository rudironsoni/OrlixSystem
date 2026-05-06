/* IXLandSystem/kernel/sys.c
 * Internal kernel misc call owner
 *
 * Canonical internal implementations for process-group and session
 * primitives that have no dedicated owner file yet.
 * The exported Linux-facing call surface lives in IXLandLibC.
 */

#include <errno.h>

#include "task.h"

/* TODO: Add any misc syscalls that don't belong to a specific subsystem owner */
