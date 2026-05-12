#include "pty.h"

#include <linux/atomic.h>
#include <linux/errno.h>
#include <uapi/linux/poll.h>
#include <uapi/linux/signal.h>
#include <linux/string.h>

#include "../kernel/signal.h"
#include "../private/kernel/signal_state.h"
#include "../kernel/task.h"
#include "../private/kernel/task_state.h"
#include "../private/kernel/wait_queue_state.h"
#include "../kernel/wait_queue.h"
#include "internal/fs/lock.h"
#include "internal/slab.h"

void poll_notify_readiness_impl(void);

#define PTY_MAX 128
#define PTY_BUFFER_CAPACITY 4096

#define PTY_LFLAG_ISIG 0x00000001U
#define PTY_LFLAG_ICANON 0x00000002U
#define PTY_LFLAG_ECHO 0x00000008U

#define PTY_CC_VINTR 0
#define PTY_CC_VQUIT 1
#define PTY_CC_VERASE 2
#define PTY_CC_VKILL 3
#define PTY_CC_VEOF 4
#define PTY_CC_VTIME 5
#define PTY_CC_VMIN 6
#define PTY_CC_VSUSP 10

typedef struct pty_ring_buffer {
    unsigned char data[PTY_BUFFER_CAPACITY];
    size_t head;
    size_t tail;
    size_t len;
} pty_ring_buffer_t;

typedef struct pty_pair {
    bool allocated;
    unsigned int master_open_count;
    unsigned int slave_open_count;
    bool slave_locked;
    bool has_controlling_session;
    int32_t controlling_sid;
    pty_linux_termios_t termios;
    pty_linux_winsize_t winsize;
    int32_t foreground_pgrp;
    pty_ring_buffer_t master_to_slave;
    pty_ring_buffer_t slave_to_master;
    unsigned char canonical_pending[PTY_BUFFER_CAPACITY];
    size_t canonical_pending_len;
    struct wait_queue_head wait;
} pty_pair_t;

static pty_pair_t pty_table[PTY_MAX];
static fs_mutex_t pty_lock = FS_MUTEX_INITIALIZER;
static atomic_t pty_next_hint = ATOMIC_INIT(0);

static void pty_ring_init(pty_ring_buffer_t *ring) {
    ring->head = 0;
    ring->tail = 0;
    ring->len = 0;
}

static size_t pty_ring_write(pty_ring_buffer_t *ring, const unsigned char *src, size_t count) {
    size_t written = 0;
    while (written < count && ring->len < PTY_BUFFER_CAPACITY) {
        ring->data[ring->tail] = src[written];
        ring->tail = (ring->tail + 1) % PTY_BUFFER_CAPACITY;
        ring->len++;
        written++;
    }
    return written;
}

static size_t pty_ring_read(pty_ring_buffer_t *ring, unsigned char *dst, size_t count) {
    size_t read_count = 0;
    while (read_count < count && ring->len > 0) {
        dst[read_count] = ring->data[ring->head];
        ring->head = (ring->head + 1) % PTY_BUFFER_CAPACITY;
        ring->len--;
        read_count++;
    }
    return read_count;
}

static void pty_ring_clear(pty_ring_buffer_t *ring) {
    ring->head = 0;
    ring->tail = 0;
    ring->len = 0;
}

static bool pty_termios_has_flag(const pty_linux_termios_t *termios, uint32_t flag) {
    return (termios->c_lflag & flag) != 0;
}

static void pty_emit_echo_byte_impl(pty_pair_t *pair, unsigned char byte) {
    if (!pty_termios_has_flag(&pair->termios, PTY_LFLAG_ECHO)) {
        return;
    }
    (void)pty_ring_write(&pair->slave_to_master, &byte, 1);
}

static void pty_emit_echo_erase_impl(pty_pair_t *pair) {
    if (!pty_termios_has_flag(&pair->termios, PTY_LFLAG_ECHO)) {
        return;
    }
    static const unsigned char seq[3] = {'\b', ' ', '\b'};
    (void)pty_ring_write(&pair->slave_to_master, seq, sizeof(seq));
}

static bool pty_flush_canonical_pending_impl(pty_pair_t *pair) {
    if (pair->canonical_pending_len == 0) {
        return true;
    }
    size_t written = pty_ring_write(&pair->master_to_slave, pair->canonical_pending, pair->canonical_pending_len);
    if (written != pair->canonical_pending_len) {
        if (written > 0) {
            memmove(pair->canonical_pending, pair->canonical_pending + written, pair->canonical_pending_len - written);
            pair->canonical_pending_len -= written;
        }
        return false;
    }
    pair->canonical_pending_len = 0;
    return true;
}

static bool pty_deliver_signal_char_impl(pty_pair_t *pair, unsigned char byte) {
    if (!pty_termios_has_flag(&pair->termios, PTY_LFLAG_ISIG)) {
        return false;
    }

    int signal_number = 0;
    if (byte == pair->termios.c_cc[PTY_CC_VINTR]) {
        signal_number = SIGINT;
    } else if (byte == pair->termios.c_cc[PTY_CC_VQUIT]) {
        signal_number = SIGQUIT;
    } else if (byte == pair->termios.c_cc[PTY_CC_VSUSP]) {
        signal_number = SIGTSTP;
    }

    if (signal_number == 0) {
        return false;
    }

    if (pair->foreground_pgrp > 0) {
        signal_generate_pgrp(pair->foreground_pgrp, signal_number);
    }
    return true;
}

static bool pty_accept_canonical_byte_impl(pty_pair_t *pair, unsigned char byte) {
    if (pty_deliver_signal_char_impl(pair, byte)) {
        return true;
    }

    if (byte == pair->termios.c_cc[PTY_CC_VERASE]) {
        if (pair->canonical_pending_len > 0) {
            pair->canonical_pending_len--;
            pty_emit_echo_erase_impl(pair);
        }
        return true;
    }

    if (byte == pair->termios.c_cc[PTY_CC_VKILL]) {
        if (pair->canonical_pending_len > 0) {
            pair->canonical_pending_len = 0;
            pty_emit_echo_byte_impl(pair, '\n');
        }
        return true;
    }

    if (byte == pair->termios.c_cc[PTY_CC_VEOF]) {
        return pty_flush_canonical_pending_impl(pair);
    }

    if (pair->canonical_pending_len >= PTY_BUFFER_CAPACITY) {
        return false;
    }

    pair->canonical_pending[pair->canonical_pending_len++] = byte;
    pty_emit_echo_byte_impl(pair, byte);

    if (byte == '\n') {
        return pty_flush_canonical_pending_impl(pair);
    }

    return true;
}

static bool pty_accept_noncanonical_byte_impl(pty_pair_t *pair, unsigned char byte) {
    if (pty_deliver_signal_char_impl(pair, byte)) {
        return true;
    }

    pty_emit_echo_byte_impl(pair, byte);
    return pty_ring_write(&pair->master_to_slave, &byte, 1) == 1;
}

static ssize_t pty_write_master_linedisc_impl(pty_pair_t *pair, const void *buf, size_t count) {
    const unsigned char *src = (const unsigned char *)buf;
    size_t accepted = 0;

    for (size_t i = 0; i < count; i++) {
        bool ok;
        if (pty_termios_has_flag(&pair->termios, PTY_LFLAG_ICANON)) {
            ok = pty_accept_canonical_byte_impl(pair, src[i]);
        } else {
            ok = pty_accept_noncanonical_byte_impl(pair, src[i]);
        }

        if (!ok) {
            break;
        }
        accepted++;
    }

    if (accepted == 0) {
        return -EAGAIN;
    }
    return (ssize_t)accepted;
}

static bool pty_valid_index(unsigned int pty_index) {
    return pty_index < PTY_MAX;
}

static bool pty_signal_is_ignored(const struct task *task, int signal_number) {
    if (!task || !task->signal || signal_number <= 0 || signal_number > KERNEL_SIG_NUM) {
        return false;
    }
    return task->signal->actions[signal_number - 1].handler == SIG_IGN;
}

static struct task *pty_lookup_task_locked(int32_t pid) {
    if (pid <= 0) {
        return NULL;
    }

    int idx = task_hash(pid);
    struct task *task = task_table[idx];
    while (task && task->pid != pid) {
        task = task->hash_next;
    }
    return task;
}

static bool pty_is_orphaned_pgrp(int32_t sid, int32_t pgid) {
    bool has_member = false;
    bool orphaned = true;

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *candidate = task_table[i];
        while (candidate) {
            if (candidate->sid == sid && candidate->pgid == pgid) {
                struct task *parent = candidate->parent;
                if (!parent && candidate->ppid > 0) {
                    parent = pty_lookup_task_locked(candidate->ppid);
                }
                has_member = true;
                if (parent && parent->sid == sid && parent->pgid != pgid) {
                    orphaned = false;
                    break;
                }
            }
            candidate = candidate->hash_next;
        }
        if (!orphaned) {
            break;
        }
    }
    kernel_mutex_unlock(&task_table_lock);

    return has_member && orphaned;
}

static void pty_clear_task_tty_refs_impl(unsigned int pty_index) {
    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *task = task_table[i];
        while (task) {
            kernel_mutex_lock(&task->lock);
            if (task->tty && task->tty->index == (int)pty_index) {
                atomic_dec(&task->tty->refs);
                task->tty = NULL;
            }
            kernel_mutex_unlock(&task->lock);
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

static void pty_clear_session_tty_refs_impl(unsigned int pty_index, int32_t sid) {
    if (sid <= 0) {
        return;
    }

    kernel_mutex_lock(&task_table_lock);
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        struct task *task = task_table[i];
        while (task) {
            kernel_mutex_lock(&task->lock);
            if (task->sid == sid && task->tty && task->tty->index == (int)pty_index) {
                atomic_dec(&task->tty->refs);
                task->tty = NULL;
            }
            kernel_mutex_unlock(&task->lock);
            task = task->hash_next;
        }
    }
    kernel_mutex_unlock(&task_table_lock);
}

static int pty_check_background_read_access(pty_pair_t *pair) {
    struct task *task = task_current();
    if (!task || !task->signal || !pair->has_controlling_session || pair->controlling_sid != task->sid) {
        return 0;
    }

    int32_t fg_pgrp = pair->foreground_pgrp;
    if (fg_pgrp <= 0 || fg_pgrp == task->pgid) {
        return 0;
    }

    if (pty_is_orphaned_pgrp(task->sid, task->pgid)) {
        return -EIO;
    }

    if (signal_is_blocked(task, SIGTTIN) || pty_signal_is_ignored(task, SIGTTIN)) {
        return -EIO;
    }

    (void)signal_generate_pgrp(task->pgid, SIGTTIN);
    return -EINTR;
}

static int pty_check_background_write_access(pty_pair_t *pair, bool enforce_background_stop, bool blocked_or_ignored_is_error) {
    struct task *task = task_current();
    if (!task || !task->signal || !pair->has_controlling_session || pair->controlling_sid != task->sid) {
        return 0;
    }

    int32_t fg_pgrp = pair->foreground_pgrp;
    if (fg_pgrp <= 0 || fg_pgrp == task->pgid) {
        return 0;
    }

    if (!enforce_background_stop) {
        return 0;
    }

    if (pty_is_orphaned_pgrp(task->sid, task->pgid)) {
        return -EIO;
    }

    bool blocked_or_ignored = signal_is_blocked(task, SIGTTOU) || pty_signal_is_ignored(task, SIGTTOU);
    if (blocked_or_ignored) {
        if (blocked_or_ignored_is_error) {
            return -EIO;
        }
        return 0;
    }

    (void)signal_generate_pgrp(task->pgid, SIGTTOU);
    return -EINTR;
}

static void pty_init_defaults(pty_pair_t *pair) {
    memset(&pair->termios, 0, sizeof(pair->termios));
    pair->termios.c_iflag = 0x00000500U;
    pair->termios.c_oflag = 0x00000005U;
    pair->termios.c_cflag = 0x000000BFU;
    pair->termios.c_lflag = 0x00008A3BU;
    pair->termios.c_cc[0] = 3;
    pair->termios.c_cc[1] = 28;
    pair->termios.c_cc[2] = 127;
    pair->termios.c_cc[3] = 21;
    pair->termios.c_cc[4] = 4;
    pair->termios.c_cc[5] = 0;
    pair->termios.c_cc[6] = 1;
    pair->termios.c_cc[8] = 17;
    pair->termios.c_cc[9] = 19;
    pair->termios.c_cc[10] = 26;

    memset(&pair->winsize, 0, sizeof(pair->winsize));
    pair->winsize.ws_row = 24;
    pair->winsize.ws_col = 80;
    pair->foreground_pgrp = 0;

    pty_ring_init(&pair->master_to_slave);
    pty_ring_init(&pair->slave_to_master);
}

int pty_allocate_pair_impl(unsigned int *pty_index) {
    if (!pty_index) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    unsigned int hint = (unsigned int)atomic_read(&pty_next_hint) % PTY_MAX;
    for (unsigned int i = 0; i < PTY_MAX; i++) {
        unsigned int idx = (hint + i) % PTY_MAX;
        if (pty_table[idx].allocated) {
            continue;
        }

        pty_pair_t *pair = &pty_table[idx];
        memset(pair, 0, sizeof(*pair));
        pair->allocated = true;
        pair->master_open_count = 1;
        pair->slave_open_count = 0;
        pair->slave_locked = true;
        pty_init_defaults(pair);
        wait_queue_init(&pair->wait);

        atomic_set(&pty_next_hint, (int)(idx + 1));
        *pty_index = idx;
        fs_mutex_unlock(&pty_lock);
        return 0;
    }

    fs_mutex_unlock(&pty_lock);
    return -EAGAIN;
}

int pty_format_slave_path_impl(unsigned int pty_index, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0 || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    static const char prefix[] = "/dev/pts/";
    size_t prefklen = sizeof(prefix) - 1;
    if (buf_len <= prefklen) {
        return -ENAMETOOLONG;
    }

    memcpy(buf, prefix, prefklen);

    char digits[16];
    size_t digit_len = 0;
    unsigned int value = pty_index;
    do {
        digits[digit_len++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && digit_len < sizeof(digits));

    if (value != 0 || (prefklen + digit_len + 1) > buf_len) {
        return -ENAMETOOLONG;
    }

    for (size_t i = 0; i < digit_len; i++) {
        buf[prefklen + i] = digits[digit_len - 1 - i];
    }
    buf[prefklen + digit_len] = '\0';
    return 0;
}

static int pty_parse_slave_path(const char *path, unsigned int *pty_index) {
    if (!path || !pty_index) {
        return -EINVAL;
    }

    static const char *prefix = "/dev/pts/";
    size_t prefklen = strlen(prefix);
    if (strncmp(path, prefix, prefklen) != 0) {
        return -ENOENT;
    }

    const char *num = path + prefklen;
    if (*num == '\0') {
        return -ENOENT;
    }

    unsigned long value = 0;
    for (const char *cursor = num; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return -ENOENT;
        }
        value = (value * 10UL) + (unsigned long)(*cursor - '0');
        if (value >= PTY_MAX) {
            return -ENOENT;
        }
    }

    *pty_index = (unsigned int)value;
    return 0;
}

int pty_lookup_slave_path_impl(const char *path, unsigned int *pty_index) {
    unsigned int idx;
    int ret = pty_parse_slave_path(path, &idx);
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[idx];
    if (!pair->allocated || pair->slave_locked) {
        fs_mutex_unlock(&pty_lock);
        return -ENOENT;
    }

    if (pty_index) {
        *pty_index = idx;
    }
    fs_mutex_unlock(&pty_lock);
    return 0;
}

bool pty_is_virtual_slave_path_impl(const char *path) {
    unsigned int idx;
    return pty_lookup_slave_path_impl(path, &idx) == 0;
}

int pty_open_controlling_slave_impl(unsigned int *pty_index) {
    if (!pty_index) {
        return -EINVAL;
    }

    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }

    kernel_mutex_lock(&task->lock);
    struct tty_state *tty = task->tty;
    if (!tty) {
        kernel_mutex_unlock(&task->lock);
        return -ENXIO;
    }
    unsigned int idx = (unsigned int)tty->index;
    kernel_mutex_unlock(&task->lock);

    if (!pty_valid_index(idx)) {
        return -ENXIO;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[idx];
    if (!pair->allocated || pair->slave_open_count == 0) {
        fs_mutex_unlock(&pty_lock);
        return -EIO;
    }

    pair->slave_open_count++;
    *pty_index = idx;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

int pty_open_slave_by_path_impl(const char *path, unsigned int *pty_index) {
    unsigned int idx;
    if (!pty_index) {
        return -EINVAL;
    }
    int ret = pty_parse_slave_path(path, &idx);
    if (ret != 0) {
        return ret;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[idx];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -ENOENT;
    }
    if (pair->slave_locked) {
        fs_mutex_unlock(&pty_lock);
        return -EIO;
    }
    pair->slave_open_count++;
    *pty_index = idx;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

size_t pty_list_slave_indices_impl(unsigned int *indices, size_t capacity) {
    size_t count = 0;

    fs_mutex_lock(&pty_lock);
    for (unsigned int idx = 0; idx < PTY_MAX; idx++) {
        pty_pair_t *pair = &pty_table[idx];
        if (!pair->allocated || pair->slave_locked) {
            continue;
        }
        if (indices && count < capacity) {
            indices[count] = idx;
        }
        count++;
    }
    fs_mutex_unlock(&pty_lock);

    return count;
}

int pty_close_end_impl(unsigned int pty_index, bool is_master) {
    bool clear_task_tty_refs = false;

    if (!pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }

    if (is_master) {
        if (pair->master_open_count > 0) {
            pair->master_open_count--;
        }
    } else {
        if (pair->slave_open_count > 0) {
            pair->slave_open_count--;
            if (pair->slave_open_count == 0 && pair->has_controlling_session) {
                pair->has_controlling_session = false;
                pair->controlling_sid = 0;
                pair->foreground_pgrp = 0;
                clear_task_tty_refs = true;
            }
        }
    }

    wait_queue_wake_all(&pair->wait);

    if (pair->master_open_count == 0 && pair->slave_open_count == 0) {
        wait_queue_destroy(&pair->wait);
        memset(pair, 0, sizeof(*pair));
    }

    fs_mutex_unlock(&pty_lock);
    poll_notify_readiness_impl();

    if (clear_task_tty_refs) {
        pty_clear_task_tty_refs_impl(pty_index);
    }
    return 0;
}

static ssize_t pty_read_from_ring(pty_ring_buffer_t *ring, bool peer_open, void *buf, size_t count,
                                  bool nonblock) {
    if (count == 0) {
        return 0;
    }
    if (ring->len == 0) {
        if (!peer_open) {
            return 0;
        }
        if (nonblock) {
            return -EAGAIN;
        }
        return -EAGAIN;
    }

    return (ssize_t)pty_ring_read(ring, (unsigned char *)buf, count);
}

static ssize_t pty_write_to_ring(pty_ring_buffer_t *ring, bool peer_open, const void *buf, size_t count,
                                 bool nonblock) {
    if (count == 0) {
        return 0;
    }
    if (!peer_open) {
        return -EIO;
    }

    size_t written = pty_ring_write(ring, (const unsigned char *)buf, count);
    if (written == 0) {
        return nonblock ? -EAGAIN : -EAGAIN;
    }

    return (ssize_t)written;
}

ssize_t pty_read_master_impl(unsigned int pty_index, void *buf, size_t count, bool nonblock) {
    if (!buf || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated || pair->master_open_count == 0) {
        fs_mutex_unlock(&pty_lock);
        return -EIO;
    }

    ssize_t ret = pty_read_from_ring(&pair->slave_to_master, pair->slave_open_count > 0, buf, count, nonblock);
    if (ret > 0) {
        wait_queue_wake_all(&pair->wait);
    }
    fs_mutex_unlock(&pty_lock);
    if (ret > 0) {
        poll_notify_readiness_impl();
    }
    return ret;
}

ssize_t pty_write_master_impl(unsigned int pty_index, const void *buf, size_t count, bool nonblock) {
    if (!buf || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated || pair->master_open_count == 0) {
        fs_mutex_unlock(&pty_lock);
        return -EIO;
    }

    if (pair->slave_open_count == 0) {
        fs_mutex_unlock(&pty_lock);
        return -EIO;
    }

    ssize_t ret = pty_write_master_linedisc_impl(pair, buf, count);
    if (ret > 0) {
        wait_queue_wake_all(&pair->wait);
    }
    fs_mutex_unlock(&pty_lock);
    if (ret > 0) {
        poll_notify_readiness_impl();
    }
    return ret;
}

ssize_t pty_read_slave_impl(unsigned int pty_index, void *buf, size_t count, bool nonblock) {
    if (!buf || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated || pair->slave_open_count == 0) {
        fs_mutex_unlock(&pty_lock);
        return -EIO;
    }

    int access_result = pty_check_background_read_access(pair);
    if (access_result != 0) {
        fs_mutex_unlock(&pty_lock);
        return access_result;
    }

    if (!pty_termios_has_flag(&pair->termios, PTY_LFLAG_ICANON)) {
        unsigned char vmin = pair->termios.c_cc[PTY_CC_VMIN];
        unsigned char vtime = pair->termios.c_cc[PTY_CC_VTIME];
        size_t available = pair->master_to_slave.len;

        if (vmin == 0) {
            if (available == 0) {
                if (vtime == 0) {
                    fs_mutex_unlock(&pty_lock);
                    return 0;
                }
                fs_mutex_unlock(&pty_lock);
                return -EAGAIN;
            }
        } else if (available < vmin) {
            fs_mutex_unlock(&pty_lock);
            return -EAGAIN;
        }
    }

    ssize_t ret = pty_read_from_ring(&pair->master_to_slave, pair->master_open_count > 0, buf, count, nonblock);
    if (ret > 0) {
        wait_queue_wake_all(&pair->wait);
    }
    fs_mutex_unlock(&pty_lock);
    if (ret > 0) {
        poll_notify_readiness_impl();
    }
    return ret;
}

ssize_t pty_write_slave_impl(unsigned int pty_index, const void *buf, size_t count, bool nonblock) {
    if (!buf || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated || pair->slave_open_count == 0) {
        fs_mutex_unlock(&pty_lock);
        return -EIO;
    }

    bool tostop = (pair->termios.c_lflag & PTY_LFLAG_TOSTOP) != 0;
    int access_result = pty_check_background_write_access(pair, tostop, false);
    if (access_result != 0) {
        fs_mutex_unlock(&pty_lock);
        return access_result;
    }

    ssize_t ret = pty_write_to_ring(&pair->slave_to_master, pair->master_open_count > 0, buf, count, nonblock);
    if (ret > 0) {
        wait_queue_wake_all(&pair->wait);
    }
    fs_mutex_unlock(&pty_lock);
    if (ret > 0) {
        poll_notify_readiness_impl();
    }
    return ret;
}

int pty_get_readable_bytes_impl(unsigned int pty_index, bool is_master, int *bytes) {
    if (!bytes || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }

    pty_ring_buffer_t *read_ring = is_master ? &pair->slave_to_master : &pair->master_to_slave;
    *bytes = (int)read_ring->len;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

short pty_poll_revents_impl(unsigned int pty_index, bool is_master, short events) {
    if (!pty_valid_index(pty_index)) {
        return POLLNVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return POLLNVAL;
    }

    pty_ring_buffer_t *read_ring = is_master ? &pair->slave_to_master : &pair->master_to_slave;
    pty_ring_buffer_t *write_ring = is_master ? &pair->master_to_slave : &pair->slave_to_master;
    bool this_open = is_master ? pair->master_open_count > 0 : pair->slave_open_count > 0;
    bool peer_open = is_master ? pair->slave_open_count > 0 : pair->master_open_count > 0;

    short revents = 0;
    if (!this_open) {
        revents |= POLLNVAL;
        fs_mutex_unlock(&pty_lock);
        return revents;
    }

    if (events & (POLLIN | POLLRDNORM)) {
        if (read_ring->len > 0 || !peer_open) {
            revents |= (events & (POLLIN | POLLRDNORM));
        }
    }

    if (events & (POLLOUT | POLLWRNORM)) {
        if (peer_open && write_ring->len < PTY_BUFFER_CAPACITY) {
            revents |= (events & (POLLOUT | POLLWRNORM));
        }
    }

    if (!peer_open) {
        revents |= POLLHUP;
    }

    fs_mutex_unlock(&pty_lock);
    return revents;
}

void pty_poll_wake_impl(unsigned int pty_index) {
    if (!pty_valid_index(pty_index)) {
        return;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (pair->allocated) {
        wait_queue_wake_all(&pair->wait);
    }
    fs_mutex_unlock(&pty_lock);
    poll_notify_readiness_impl();
}

int pty_set_lock_impl(unsigned int pty_index, bool locked) {
    if (!pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }
    pair->slave_locked = locked;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

int pty_get_lock_impl(unsigned int pty_index, int *locked) {
    if (!locked || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }
    *locked = pair->slave_locked ? 1 : 0;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

int pty_get_termios_impl(unsigned int pty_index, pty_linux_termios_t *termios) {
    if (!termios || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }
    *termios = pair->termios;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

int pty_set_termios_with_action_impl(unsigned int pty_index, const pty_linux_termios_t *termios, int action) {
    if (!termios || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }
    if (action != PTY_TCSET_ACTION_NOW && action != PTY_TCSET_ACTION_DRAIN && action != PTY_TCSET_ACTION_FLUSH) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }

    int access_result = pty_check_background_write_access(pair, true, false);
    if (access_result != 0) {
        fs_mutex_unlock(&pty_lock);
        return access_result;
    }

    if (action == PTY_TCSET_ACTION_FLUSH) {
        pty_ring_clear(&pair->master_to_slave);
        pair->canonical_pending_len = 0;
    }

    pair->termios = *termios;
    wait_queue_wake_all(&pair->wait);
    fs_mutex_unlock(&pty_lock);
    poll_notify_readiness_impl();
    return 0;
}

int pty_set_termios_impl(unsigned int pty_index, const pty_linux_termios_t *termios) {
    return pty_set_termios_with_action_impl(pty_index, termios, PTY_TCSET_ACTION_NOW);
}

int pty_get_winsize_impl(unsigned int pty_index, pty_linux_winsize_t *winsize) {
    if (!winsize || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }
    *winsize = pair->winsize;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

int pty_set_winsize_impl(unsigned int pty_index, const pty_linux_winsize_t *winsize) {
    if (!winsize || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    int32_t foreground_pgrp = 0;
    int changed = 0;

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }

    if (memcmp(&pair->winsize, winsize, sizeof(*winsize)) != 0) {
        changed = 1;
    }
    pair->winsize = *winsize;
    foreground_pgrp = pair->foreground_pgrp;
    fs_mutex_unlock(&pty_lock);

    if (changed && foreground_pgrp > 0) {
        signal_generate_pgrp(foreground_pgrp, SIGWINCH);
    }
    if (changed) {
        pty_poll_wake_impl(pty_index);
    }

    return 0;
}

int pty_set_controlling_tty_impl(unsigned int pty_index, int arg) {
    if (!pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }

    if (arg != 0) {
        return -EPERM;
    }

    kernel_mutex_lock(&task->lock);

    if (task->sid <= 0 || task->pid != task->sid) {
        kernel_mutex_unlock(&task->lock);
        return -EPERM;
    }

    if (task->tty && task->tty->index == (int)pty_index) {
        kernel_mutex_unlock(&task->lock);
        return 0;
    }

    if (task->tty) {
        kernel_mutex_unlock(&task->lock);
        return -EPERM;
    }

    struct tty_state *tty = __kmalloc_noprof(sizeof(*tty), GFP_KERNEL);
    if (!tty) {
        kernel_mutex_unlock(&task->lock);
        return -ENOMEM;
    }
    memset(tty, 0, sizeof(*tty));

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        kernel_mutex_unlock(&task->lock);
        kfree(tty);
        return -EINVAL;
    }

    if (pair->has_controlling_session && pair->controlling_sid != task->sid) {
        fs_mutex_unlock(&pty_lock);
        kernel_mutex_unlock(&task->lock);
        kfree(tty);
        return -EPERM;
    }

    pair->has_controlling_session = true;
    pair->controlling_sid = task->sid;
    pair->foreground_pgrp = task->pgid;
    fs_mutex_unlock(&pty_lock);

    tty->index = (int)pty_index;
    tty->foreground_pgrp = task->pgid;
    atomic_set(&tty->refs, 1);
    task->tty = tty;

    kernel_mutex_unlock(&task->lock);
    return 0;
}

int pty_get_foreground_pgrp_impl(unsigned int pty_index, int32_t *pgrp) {
    if (!pgrp || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }

    if (!pair->has_controlling_session || pair->controlling_sid != task->sid) {
        fs_mutex_unlock(&pty_lock);
        return -ENOTTY;
    }

    *pgrp = pair->foreground_pgrp;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

int pty_get_controlling_sid_impl(unsigned int pty_index, int32_t *sid) {
    if (!sid || !pty_valid_index(pty_index)) {
        return -EINVAL;
    }

    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }

    if (!pair->has_controlling_session || pair->controlling_sid != task->sid) {
        fs_mutex_unlock(&pty_lock);
        return -ENOTTY;
    }

    *sid = pair->controlling_sid;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

int pty_set_foreground_pgrp_impl(unsigned int pty_index, int32_t pgrp) {
    if (!pty_valid_index(pty_index) || pgrp <= 0) {
        return -EINVAL;
    }

    struct task *task = task_current();
    if (!task) {
        return -ESRCH;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (!pair->allocated) {
        fs_mutex_unlock(&pty_lock);
        return -EINVAL;
    }

    if (!pair->has_controlling_session || pair->controlling_sid != task->sid) {
        fs_mutex_unlock(&pty_lock);
        return -ENOTTY;
    }

    if (!task_session_has_pgrp_impl(task->sid, pgrp)) {
        fs_mutex_unlock(&pty_lock);
        return -EPERM;
    }

    if (pair->foreground_pgrp > 0 && pair->foreground_pgrp != task->pgid) {
        if (!pty_signal_is_ignored(task, SIGTTOU) && !signal_is_blocked(task, SIGTTOU)) {
            (void)signal_generate_pgrp(task->pgid, SIGTTOU);
            fs_mutex_unlock(&pty_lock);
            return -EINTR;
        }
    }

    pair->foreground_pgrp = pgrp;
    fs_mutex_unlock(&pty_lock);
    return 0;
}

int pty_detach_controlling_tty_impl(void) {
    struct task *task = task_current();
    int32_t foreground_pgrp = 0;
    bool session_leader = false;
    if (!task) {
        return -ESRCH;
    }

    kernel_mutex_lock(&task->lock);

    if (!task->tty) {
        kernel_mutex_unlock(&task->lock);
        return -ENOTTY;
    }

    unsigned int pty_index = (unsigned int)task->tty->index;
    int32_t current_sid = task->sid;
    session_leader = task->pid == task->sid && task->sid > 0;

    atomic_dec(&task->tty->refs);
    task->tty = NULL;

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (pair->allocated && pair->has_controlling_session && pair->controlling_sid == current_sid) {
        foreground_pgrp = pair->foreground_pgrp;
        pair->has_controlling_session = false;
        pair->controlling_sid = 0;
        pair->foreground_pgrp = 0;
        wait_queue_wake_all(&pair->wait);
    }
    fs_mutex_unlock(&pty_lock);

    kernel_mutex_unlock(&task->lock);
    if (session_leader) {
        pty_clear_session_tty_refs_impl(pty_index, current_sid);
        if (foreground_pgrp > 0) {
            (void)signal_generate_pgrp(foreground_pgrp, SIGHUP);
            (void)signal_generate_pgrp(foreground_pgrp, SIGCONT);
        }
        poll_notify_readiness_impl();
    }
    return 0;
}

void pty_session_leader_exit_impl(struct task *task) {
    unsigned int pty_index;
    int32_t sid;
    int32_t foreground_pgrp = 0;
    bool has_tty = false;

    if (!task) {
        return;
    }

    kernel_mutex_lock(&task->lock);
    if (task->sid <= 0 || task->pid != task->sid || !task->tty) {
        kernel_mutex_unlock(&task->lock);
        return;
    }
    pty_index = (unsigned int)task->tty->index;
    sid = task->sid;
    has_tty = true;
    kernel_mutex_unlock(&task->lock);

    if (!has_tty || !pty_valid_index(pty_index)) {
        return;
    }

    fs_mutex_lock(&pty_lock);
    pty_pair_t *pair = &pty_table[pty_index];
    if (pair->allocated && pair->has_controlling_session && pair->controlling_sid == sid) {
        foreground_pgrp = pair->foreground_pgrp;
        pair->has_controlling_session = false;
        pair->controlling_sid = 0;
        pair->foreground_pgrp = 0;
        wait_queue_wake_all(&pair->wait);
    }
    fs_mutex_unlock(&pty_lock);

    pty_clear_session_tty_refs_impl(pty_index, sid);

    if (foreground_pgrp > 0) {
        (void)signal_generate_pgrp(foreground_pgrp, SIGHUP);
        (void)signal_generate_pgrp(foreground_pgrp, SIGCONT);
    }
    poll_notify_readiness_impl();
}
