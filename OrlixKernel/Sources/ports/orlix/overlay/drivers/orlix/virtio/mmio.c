// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/limits.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/virtio_config.h>
#include <linux/workqueue.h>
#include <linux/virtio_ids.h>
#include <uapi/linux/virtio_blk.h>
#include <uapi/linux/virtio_mmio.h>
#include <uapi/linux/virtio_ring.h>
#include <asm/page.h>
#include <internal/asm/host_block.h>
#include <internal/asm/host_console.h>
#include <internal/asm/host_entropy.h>
#include <internal/asm/irq.h>
#include <internal/asm/virtio_mmio.h>

#define ORLIX_VIRTIO_MMIO_MAGIC ('v' | ('i' << 8) | ('r' << 16) | ('t' << 24))
#define ORLIX_VIRTIO_MMIO_VENDOR 0x4f524c58U
#define ORLIX_VIRTIO_MMIO_SLOT_SIZE 0x200UL
#define ORLIX_VIRTIO_MMIO_QUEUE_COUNT 2
#define ORLIX_VIRTIO_MMIO_QUEUE_SIZE 128
#define ORLIX_VIRTIO_MMIO_BLOCK_SECTOR_SIZE 512ULL
#define ORLIX_VIRTIO_MMIO_CONSOLE_INPUT_POLL_MS 10
#define ORLIX_VIRTIO_BLK_BASE_FEATURES \
	((1ULL << VIRTIO_F_VERSION_1) | (1ULL << VIRTIO_RING_F_INDIRECT_DESC))
#define ORLIX_VIRTIO_CONSOLE_BASE_FEATURES (1ULL << VIRTIO_F_VERSION_1)
#define ORLIX_VIRTIO_RNG_BASE_FEATURES (1ULL << VIRTIO_F_VERSION_1)

struct orlix_virtio_mmio_queue {
	u32 num;
	u32 ready;
	u64 desc;
	u64 avail;
	u64 used;
	u16 last_avail;
};

struct orlix_virtio_mmio_slot {
	unsigned long base;
	unsigned int irq;
	u32 device_id;
	unsigned int host_block_device;
	bool read_only;
	const char *device_identifier;
	u32 status;
	u32 interrupt_status;
	u32 device_features_sel;
	u32 driver_features_sel;
	u32 driver_features[2];
	u32 queue_sel;
	u32 shm_sel;
	struct orlix_virtio_mmio_queue queues[ORLIX_VIRTIO_MMIO_QUEUE_COUNT];
	struct work_struct notify_work;
	unsigned long pending_queues;
	bool notify_work_initialized;
	struct timer_list console_input_timer;
	bool console_input_timer_initialized;
};

struct orlix_virtio_mmio_desc_chain {
	struct vring_desc *desc;
	unsigned int head;
	unsigned int count;
};

static struct orlix_virtio_mmio_slot orlix_virtio_mmio_slots[] = {
	{
		.base = 0x10001000UL,
		.irq = 32,
		.device_id = VIRTIO_ID_BLOCK,
		.host_block_device = 0,
		.read_only = true,
		.device_identifier = "orlix-base-block0",
	},
	{
		.base = 0x10001200UL,
		.irq = 33,
		.device_id = VIRTIO_ID_BLOCK,
		.host_block_device = 1,
		.device_identifier = "orlix-state-block1",
	},
	{
		.base = 0x10001400UL,
		.irq = 34,
		.device_id = VIRTIO_ID_CONSOLE,
		.device_identifier = "orlix-console0",
	},
	{
		.base = 0x10001600UL,
		.irq = 35,
		.device_id = VIRTIO_ID_RNG,
		.device_identifier = "orlix-rng0",
	},
};

static struct orlix_virtio_mmio_slot *
orlix_virtio_mmio_find_slot(unsigned long physical_address,
			    unsigned long *register_offset)
{
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(orlix_virtio_mmio_slots); index++) {
		struct orlix_virtio_mmio_slot *slot =
			&orlix_virtio_mmio_slots[index];

		if (physical_address >= slot->base &&
		    physical_address < slot->base + ORLIX_VIRTIO_MMIO_SLOT_SIZE) {
			*register_offset = physical_address - slot->base;
			return slot;
		}
	}

	return NULL;
}

static struct orlix_virtio_mmio_queue *
orlix_virtio_mmio_selected_queue(struct orlix_virtio_mmio_slot *slot)
{
	if (slot->queue_sel >= ARRAY_SIZE(slot->queues))
		return NULL;

	return &slot->queues[slot->queue_sel];
}

static bool
orlix_virtio_mmio_block_capacity(const struct orlix_virtio_mmio_slot *slot,
				 unsigned long long *sectors)
{
	return slot->device_id == VIRTIO_ID_BLOCK &&
	       orlix_host_block_capacity(slot->host_block_device, sectors) == 0;
}

static bool
orlix_virtio_mmio_device_present(const struct orlix_virtio_mmio_slot *slot)
{
	unsigned long long sectors;

	switch (slot->device_id) {
	case VIRTIO_ID_BLOCK:
		return orlix_virtio_mmio_block_capacity(slot, &sectors);
	case VIRTIO_ID_CONSOLE:
	case VIRTIO_ID_RNG:
		return true;
	default:
		return false;
	}
}

static u64 orlix_virtio_mmio_device_features(
	const struct orlix_virtio_mmio_slot *slot)
{
	unsigned long long sectors;

	switch (slot->device_id) {
	case VIRTIO_ID_BLOCK:
		if (!orlix_virtio_mmio_block_capacity(slot, &sectors))
			return 0;
		return ORLIX_VIRTIO_BLK_BASE_FEATURES |
		       (slot->read_only ? (1ULL << VIRTIO_BLK_F_RO) : 0);
	case VIRTIO_ID_CONSOLE:
		return ORLIX_VIRTIO_CONSOLE_BASE_FEATURES;
	case VIRTIO_ID_RNG:
		return ORLIX_VIRTIO_RNG_BASE_FEATURES;
	default:
		return 0;
	}
}

static void *orlix_virtio_mmio_guest_ptr(u64 address, u32 length)
{
	if (!address || address > ULONG_MAX || length > ULONG_MAX - address)
		return NULL;

	return __va((unsigned long)address);
}

static u16 orlix_vring_read16(__virtio16 value)
{
	return le16_to_cpu((__force __le16)value);
}

static u32 orlix_vring_read32(__virtio32 value)
{
	return le32_to_cpu((__force __le32)value);
}

static u64 orlix_vring_read64(__virtio64 value)
{
	return le64_to_cpu((__force __le64)value);
}

static void orlix_vring_write16(__virtio16 *target, u16 value)
{
	*target = (__force __virtio16)cpu_to_le16(value);
}

static void orlix_vring_write32(__virtio32 *target, u32 value)
{
	*target = (__force __virtio32)cpu_to_le32(value);
}

static bool orlix_virtio_mmio_resolve_desc_chain(
	struct vring_desc *queue_desc,
	unsigned int queue_num,
	unsigned int head,
	struct orlix_virtio_mmio_desc_chain *chain)
{
	u16 flags;
	u32 length;
	void *indirect;

	if (!chain || head >= queue_num)
		return false;

	flags = orlix_vring_read16(queue_desc[head].flags);
	if (!(flags & VRING_DESC_F_INDIRECT)) {
		chain->desc = queue_desc;
		chain->head = head;
		chain->count = queue_num;
		return true;
	}

	length = orlix_vring_read32(queue_desc[head].len);
	if (!length || length % sizeof(struct vring_desc))
		return false;

	indirect = orlix_virtio_mmio_guest_ptr(
		orlix_vring_read64(queue_desc[head].addr), length);
	if (!indirect)
		return false;

	chain->desc = indirect;
	chain->head = 0;
	chain->count = length / sizeof(struct vring_desc);
	return chain->count > 0;
}

static u8 orlix_virtio_mmio_process_block_data(
	const struct orlix_virtio_mmio_slot *slot,
	u32 type,
	u64 sector,
	const struct vring_desc *desc,
	unsigned int desc_index,
	unsigned int *written)
{
	void *buffer;
	u32 length = orlix_vring_read32(desc[desc_index].len);
	u16 flags = orlix_vring_read16(desc[desc_index].flags);
	u64 address = orlix_vring_read64(desc[desc_index].addr);

	if (flags & VRING_DESC_F_INDIRECT)
		return VIRTIO_BLK_S_IOERR;

	buffer = orlix_virtio_mmio_guest_ptr(address, length);
	if (!buffer)
		return VIRTIO_BLK_S_IOERR;

	switch (type) {
	case VIRTIO_BLK_T_IN:
		if (!(flags & VRING_DESC_F_WRITE))
			return VIRTIO_BLK_S_IOERR;
		if (orlix_host_block_read(slot->host_block_device, sector,
					  buffer, length) != 0)
			return VIRTIO_BLK_S_IOERR;
		*written += length;
		return VIRTIO_BLK_S_OK;
	case VIRTIO_BLK_T_OUT:
		if (flags & VRING_DESC_F_WRITE)
			return VIRTIO_BLK_S_IOERR;
		if (orlix_host_block_write(slot->host_block_device, sector,
					   buffer, length) != 0)
			return VIRTIO_BLK_S_UNSUPP;
		return VIRTIO_BLK_S_OK;
	case VIRTIO_BLK_T_GET_ID:
		if (!(flags & VRING_DESC_F_WRITE))
			return VIRTIO_BLK_S_IOERR;
		memset(buffer, 0, length);
		strscpy(buffer, slot->device_identifier, length);
		*written += length;
		return VIRTIO_BLK_S_OK;
	default:
		return VIRTIO_BLK_S_UNSUPP;
	}
}

static u8 orlix_virtio_mmio_process_block_request(
	const struct orlix_virtio_mmio_slot *slot,
	struct vring_desc *desc,
	unsigned int head,
	unsigned int desc_count,
	unsigned int *written)
{
	struct orlix_virtio_mmio_desc_chain chain;
	struct virtio_blk_outhdr *header;
	u16 flags;
	u32 type;
	u64 sector;
	unsigned int descriptor_index = head;
	unsigned int status_index;
	unsigned int guard;
	u8 *status;
	u8 request_status = VIRTIO_BLK_S_OK;

	if (!orlix_virtio_mmio_resolve_desc_chain(desc, desc_count, head, &chain))
		return VIRTIO_BLK_S_IOERR;

	desc = chain.desc;
	descriptor_index = chain.head;

	flags = orlix_vring_read16(desc[descriptor_index].flags);
	if ((flags & (VRING_DESC_F_WRITE | VRING_DESC_F_INDIRECT)) ||
	    !(flags & VRING_DESC_F_NEXT))
		return VIRTIO_BLK_S_IOERR;

	header = orlix_virtio_mmio_guest_ptr(
		orlix_vring_read64(desc[descriptor_index].addr),
		orlix_vring_read32(desc[descriptor_index].len));
	if (!header)
		return VIRTIO_BLK_S_IOERR;

	type = orlix_vring_read32(header->type);
	sector = orlix_vring_read64(header->sector);
	descriptor_index = orlix_vring_read16(desc[descriptor_index].next);
	if (descriptor_index >= chain.count)
		return VIRTIO_BLK_S_IOERR;

	for (guard = 0; guard < chain.count; guard++) {
		if (descriptor_index >= chain.count)
			return VIRTIO_BLK_S_IOERR;

		flags = orlix_vring_read16(desc[descriptor_index].flags);
		if (flags & VRING_DESC_F_INDIRECT)
			return VIRTIO_BLK_S_IOERR;
		if (!(flags & VRING_DESC_F_NEXT))
			break;

		if (request_status == VIRTIO_BLK_S_OK) {
			u8 result = orlix_virtio_mmio_process_block_data(
				slot, type, sector, desc, descriptor_index, written);
			if (result != VIRTIO_BLK_S_OK)
				request_status = result;
		}

		if (request_status == VIRTIO_BLK_S_OK)
			sector += orlix_vring_read32(desc[descriptor_index].len) /
				  ORLIX_VIRTIO_MMIO_BLOCK_SECTOR_SIZE;
		descriptor_index = orlix_vring_read16(desc[descriptor_index].next);
	}

	if (guard == chain.count)
		return VIRTIO_BLK_S_IOERR;

	status_index = descriptor_index;
	flags = orlix_vring_read16(desc[status_index].flags);
	if (!(flags & VRING_DESC_F_WRITE) ||
	    (flags & (VRING_DESC_F_NEXT | VRING_DESC_F_INDIRECT)) ||
	    orlix_vring_read32(desc[status_index].len) < 1)
		return VIRTIO_BLK_S_IOERR;

	status = orlix_virtio_mmio_guest_ptr(
		orlix_vring_read64(desc[status_index].addr), 1);
	if (!status)
		return VIRTIO_BLK_S_IOERR;

	if (type != VIRTIO_BLK_T_IN && type != VIRTIO_BLK_T_OUT &&
	    type != VIRTIO_BLK_T_GET_ID && type != VIRTIO_BLK_T_FLUSH)
		request_status = VIRTIO_BLK_S_UNSUPP;

	*status = request_status;

	(*written)++;
	return request_status;
}

static void orlix_virtio_mmio_push_used(
	struct orlix_virtio_mmio_slot *slot,
	struct orlix_virtio_mmio_queue *queue,
	unsigned int head,
	unsigned int written)
{
	struct vring_used *used = orlix_virtio_mmio_guest_ptr(
		queue->used, sizeof(*used));
	u16 used_index;

	if (!used)
		return;

	used_index = orlix_vring_read16(used->idx);
	orlix_vring_write32(&used->ring[used_index % queue->num].id, head);
	orlix_vring_write32(&used->ring[used_index % queue->num].len, written);
	orlix_vring_write16(&used->idx, used_index + 1);
	slot->interrupt_status |= VIRTIO_MMIO_INT_VRING;
	orlix_irq_dispatch(slot->irq);
}

static bool orlix_virtio_mmio_process_console_input_desc(
	const struct vring_desc *desc,
	unsigned int head,
	unsigned int *written)
{
	unsigned int descriptor_index = head;
	unsigned int guard;

	if (head >= ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
		return true;

	for (guard = 0; guard < ORLIX_VIRTIO_MMIO_QUEUE_SIZE; guard++) {
		void *buffer;
		u32 length;
		u16 flags;
		u64 address;
		unsigned long copied;

		if (descriptor_index >= ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
			return true;

		length = orlix_vring_read32(desc[descriptor_index].len);
		flags = orlix_vring_read16(desc[descriptor_index].flags);
		address = orlix_vring_read64(desc[descriptor_index].addr);

		if (!(flags & VRING_DESC_F_WRITE))
			return true;

		buffer = orlix_virtio_mmio_guest_ptr(address, length);
		if (!buffer)
			return true;

		copied = orlix_host_console_read_input(buffer, length);
		*written += copied;
		if (copied < length)
			return *written > 0;

		if (!(flags & VRING_DESC_F_NEXT))
			return true;

		descriptor_index = orlix_vring_read16(desc[descriptor_index].next);
	}

	return true;
}

static void orlix_virtio_mmio_process_console_input_queue(
	struct orlix_virtio_mmio_slot *slot)
{
	struct orlix_virtio_mmio_queue *queue = &slot->queues[0];
	struct vring_desc *desc;
	struct vring_avail *avail;

	if (!queue->ready || !queue->num || queue->num > ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
		return;

	desc = orlix_virtio_mmio_guest_ptr(
		queue->desc, sizeof(struct vring_desc) * queue->num);
	avail = orlix_virtio_mmio_guest_ptr(queue->avail, sizeof(*avail));
	if (!desc || !avail)
		return;

	while (queue->last_avail != orlix_vring_read16(avail->idx)) {
		unsigned int head =
			orlix_vring_read16(avail->ring[queue->last_avail % queue->num]);
		unsigned int written = 0;

		if (!orlix_virtio_mmio_process_console_input_desc(desc, head,
								  &written))
			break;

		orlix_virtio_mmio_push_used(slot, queue, head, written);
		queue->last_avail++;
	}
}

static void orlix_virtio_mmio_console_input_timer(struct timer_list *timer)
{
	struct orlix_virtio_mmio_slot *slot =
		from_timer(slot, timer, console_input_timer);

	orlix_virtio_mmio_process_console_input_queue(slot);
	if (slot->queues[0].ready)
		mod_timer(&slot->console_input_timer,
			  jiffies + msecs_to_jiffies(ORLIX_VIRTIO_MMIO_CONSOLE_INPUT_POLL_MS));
}

static void orlix_virtio_mmio_start_console_input_timer(
	struct orlix_virtio_mmio_slot *slot)
{
	if (slot->device_id != VIRTIO_ID_CONSOLE)
		return;

	if (!slot->console_input_timer_initialized) {
		timer_setup(&slot->console_input_timer,
			    orlix_virtio_mmio_console_input_timer, 0);
		slot->console_input_timer_initialized = true;
	}

	mod_timer(&slot->console_input_timer,
		  jiffies + msecs_to_jiffies(ORLIX_VIRTIO_MMIO_CONSOLE_INPUT_POLL_MS));
}

static void orlix_virtio_mmio_process_console_output_desc(
	const struct vring_desc *desc,
	unsigned int head,
	unsigned int *written)
{
	unsigned int descriptor_index = head;
	unsigned int guard;

	if (head >= ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
		return;

	for (guard = 0; guard < ORLIX_VIRTIO_MMIO_QUEUE_SIZE; guard++) {
		void *buffer;
		u32 length;
		u16 flags;
		u64 address;

		if (descriptor_index >= ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
			return;

		length = orlix_vring_read32(desc[descriptor_index].len);
		flags = orlix_vring_read16(desc[descriptor_index].flags);
		address = orlix_vring_read64(desc[descriptor_index].addr);

		if (flags & VRING_DESC_F_WRITE)
			return;

		buffer = orlix_virtio_mmio_guest_ptr(address, length);
		if (!buffer)
			return;

		orlix_host_console_write(buffer, length);
		*written += length;

		if (!(flags & VRING_DESC_F_NEXT))
			return;

		descriptor_index = orlix_vring_read16(desc[descriptor_index].next);
	}
}

static void orlix_virtio_mmio_process_console_output_queue(
	struct orlix_virtio_mmio_slot *slot,
	u32 queue_index)
{
	struct orlix_virtio_mmio_queue *queue;
	struct vring_desc *desc;
	struct vring_avail *avail;

	if (queue_index >= ARRAY_SIZE(slot->queues))
		return;

	queue = &slot->queues[queue_index];
	if (!queue->ready || !queue->num || queue->num > ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
		return;

	desc = orlix_virtio_mmio_guest_ptr(
		queue->desc, sizeof(struct vring_desc) * queue->num);
	avail = orlix_virtio_mmio_guest_ptr(queue->avail, sizeof(*avail));
	if (!desc || !avail)
		return;

	while (queue->last_avail != orlix_vring_read16(avail->idx)) {
		unsigned int head =
			orlix_vring_read16(avail->ring[queue->last_avail % queue->num]);
		unsigned int written = 0;

		orlix_virtio_mmio_process_console_output_desc(desc, head,
							      &written);
		orlix_virtio_mmio_push_used(slot, queue, head, written);
		queue->last_avail++;
	}
}

static void orlix_virtio_mmio_process_rng_desc(
	const struct vring_desc *desc,
	unsigned int head,
	unsigned int *written)
{
	unsigned int descriptor_index = head;
	unsigned int guard;

	if (head >= ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
		return;

	for (guard = 0; guard < ORLIX_VIRTIO_MMIO_QUEUE_SIZE; guard++) {
		void *buffer;
		u32 length;
		u16 flags;
		u64 address;
		unsigned long copied;

		if (descriptor_index >= ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
			return;

		length = orlix_vring_read32(desc[descriptor_index].len);
		flags = orlix_vring_read16(desc[descriptor_index].flags);
		address = orlix_vring_read64(desc[descriptor_index].addr);

		if (!(flags & VRING_DESC_F_WRITE))
			return;

		buffer = orlix_virtio_mmio_guest_ptr(address, length);
		if (!buffer)
			return;

		copied = orlix_host_entropy_read(buffer, length);
		*written += copied;
		if (copied < length)
			return;

		if (!(flags & VRING_DESC_F_NEXT))
			return;

		descriptor_index = orlix_vring_read16(desc[descriptor_index].next);
	}
}

static void orlix_virtio_mmio_process_rng_queue(
	struct orlix_virtio_mmio_slot *slot,
	u32 queue_index)
{
	struct orlix_virtio_mmio_queue *queue;
	struct vring_desc *desc;
	struct vring_avail *avail;

	if (queue_index != 0 || queue_index >= ARRAY_SIZE(slot->queues))
		return;

	queue = &slot->queues[queue_index];
	if (!queue->ready || !queue->num || queue->num > ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
		return;

	desc = orlix_virtio_mmio_guest_ptr(
		queue->desc, sizeof(struct vring_desc) * queue->num);
	avail = orlix_virtio_mmio_guest_ptr(queue->avail, sizeof(*avail));
	if (!desc || !avail)
		return;

	while (queue->last_avail != orlix_vring_read16(avail->idx)) {
		unsigned int head =
			orlix_vring_read16(avail->ring[queue->last_avail % queue->num]);
		unsigned int written = 0;

		orlix_virtio_mmio_process_rng_desc(desc, head, &written);
		orlix_virtio_mmio_push_used(slot, queue, head, written);
		queue->last_avail++;
	}
}

static void orlix_virtio_mmio_process_queue(
	struct orlix_virtio_mmio_slot *slot,
	u32 queue_index)
{
	struct orlix_virtio_mmio_queue *queue;
	struct vring_desc *desc;
	struct vring_avail *avail;

	if (queue_index >= ARRAY_SIZE(slot->queues))
		return;

	if (slot->device_id == VIRTIO_ID_CONSOLE) {
		if (queue_index == 0) {
			orlix_virtio_mmio_process_console_input_queue(slot);
			orlix_virtio_mmio_start_console_input_timer(slot);
		} else if (queue_index == 1) {
			orlix_virtio_mmio_process_console_output_queue(slot,
								       queue_index);
		}
		return;
	}

	if (slot->device_id == VIRTIO_ID_RNG) {
		orlix_virtio_mmio_process_rng_queue(slot, queue_index);
		return;
	}

	queue = &slot->queues[queue_index];
	if (!queue->ready || !queue->num || queue->num > ORLIX_VIRTIO_MMIO_QUEUE_SIZE)
		return;

	desc = orlix_virtio_mmio_guest_ptr(
		queue->desc, sizeof(struct vring_desc) * queue->num);
	avail = orlix_virtio_mmio_guest_ptr(queue->avail, sizeof(*avail));
	if (!desc || !avail)
		return;

	while (queue->last_avail != orlix_vring_read16(avail->idx)) {
		unsigned int head =
			orlix_vring_read16(avail->ring[queue->last_avail % queue->num]);
		unsigned int written = 0;
		u8 result;

		result = orlix_virtio_mmio_process_block_request(slot, desc,
								 head, queue->num,
								 &written);
		if (result != VIRTIO_BLK_S_OK)
			written = 1;

		orlix_virtio_mmio_push_used(slot, queue, head, written);
		queue->last_avail++;
	}
}

static void orlix_virtio_mmio_notify_work(struct work_struct *work)
{
	struct orlix_virtio_mmio_slot *slot =
		container_of(work, struct orlix_virtio_mmio_slot, notify_work);
	unsigned int queue_index;
	unsigned long pending;

	do {
		pending = xchg(&slot->pending_queues, 0);
		for (queue_index = 0;
		     queue_index < ORLIX_VIRTIO_MMIO_QUEUE_COUNT;
		     queue_index++) {
			if (pending & BIT(queue_index))
				orlix_virtio_mmio_process_queue(slot, queue_index);
		}
	} while (READ_ONCE(slot->pending_queues));
}

static void orlix_virtio_mmio_schedule_queue(
	struct orlix_virtio_mmio_slot *slot,
	u32 queue_index)
{
	if (queue_index >= ORLIX_VIRTIO_MMIO_QUEUE_COUNT)
		return;

	if (slot->device_id != VIRTIO_ID_BLOCK) {
		orlix_virtio_mmio_process_queue(slot, queue_index);
		return;
	}

	if (!slot->notify_work_initialized) {
		INIT_WORK(&slot->notify_work, orlix_virtio_mmio_notify_work);
		slot->notify_work_initialized = true;
	}

	set_bit(queue_index, &slot->pending_queues);
	schedule_work(&slot->notify_work);
}

bool orlix_virtio_mmio_read32(unsigned long physical_address, u32 *value)
{
	struct orlix_virtio_mmio_slot *slot;
	struct orlix_virtio_mmio_queue *queue;
	unsigned long offset;
	unsigned long long sectors;
	u64 features;

	slot = orlix_virtio_mmio_find_slot(physical_address, &offset);
	if (!slot)
		return false;

	queue = orlix_virtio_mmio_selected_queue(slot);

	switch (offset) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		*value = ORLIX_VIRTIO_MMIO_MAGIC;
		break;
	case VIRTIO_MMIO_VERSION:
		*value = 2;
		break;
	case VIRTIO_MMIO_DEVICE_ID:
		*value = orlix_virtio_mmio_device_present(slot) ?
			 slot->device_id : 0;
		break;
	case VIRTIO_MMIO_VENDOR_ID:
		*value = ORLIX_VIRTIO_MMIO_VENDOR;
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		features = orlix_virtio_mmio_device_features(slot);
		*value = slot->device_features_sel < 2 ?
			 (u32)(features >> (slot->device_features_sel * 32)) : 0;
		break;
	case VIRTIO_MMIO_QUEUE_NUM_MAX:
		*value = queue ? ORLIX_VIRTIO_MMIO_QUEUE_SIZE : 0;
		break;
	case VIRTIO_MMIO_QUEUE_NUM:
		*value = queue ? queue->num : 0;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		*value = queue ? queue->ready : 0;
		break;
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		*value = slot->interrupt_status;
		break;
	case VIRTIO_MMIO_STATUS:
		*value = slot->status;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		*value = queue ? (u32)queue->desc : 0;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		*value = queue ? (u32)(queue->desc >> 32) : 0;
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		*value = queue ? (u32)queue->avail : 0;
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		*value = queue ? (u32)(queue->avail >> 32) : 0;
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		*value = queue ? (u32)queue->used : 0;
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		*value = queue ? (u32)(queue->used >> 32) : 0;
		break;
	case VIRTIO_MMIO_CONFIG + offsetof(struct virtio_blk_config, capacity):
		*value = orlix_virtio_mmio_block_capacity(slot, &sectors) ?
			 (u32)sectors : 0;
		break;
	case VIRTIO_MMIO_CONFIG + offsetof(struct virtio_blk_config, capacity) + 4:
		*value = orlix_virtio_mmio_block_capacity(slot, &sectors) ?
			 (u32)(sectors >> 32) : 0;
		break;
	case VIRTIO_MMIO_SHM_LEN_LOW:
	case VIRTIO_MMIO_SHM_LEN_HIGH:
		*value = ~0U;
		break;
	case VIRTIO_MMIO_SHM_BASE_LOW:
	case VIRTIO_MMIO_SHM_BASE_HIGH:
	case VIRTIO_MMIO_CONFIG_GENERATION:
	default:
		*value = 0;
		break;
	}

	return true;
}

bool orlix_virtio_mmio_write32(unsigned long physical_address, u32 value)
{
	struct orlix_virtio_mmio_slot *slot;
	struct orlix_virtio_mmio_queue *queue;
	unsigned long offset;

	slot = orlix_virtio_mmio_find_slot(physical_address, &offset);
	if (!slot)
		return false;

	queue = orlix_virtio_mmio_selected_queue(slot);

	switch (offset) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		slot->device_features_sel = value;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		if (slot->driver_features_sel < ARRAY_SIZE(slot->driver_features))
			slot->driver_features[slot->driver_features_sel] = value;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		slot->driver_features_sel = value;
		break;
	case VIRTIO_MMIO_QUEUE_SEL:
		slot->queue_sel = value;
		queue = orlix_virtio_mmio_selected_queue(slot);
		break;
	case VIRTIO_MMIO_QUEUE_NUM:
		if (queue)
			queue->num = value;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		if (queue) {
			queue->ready = value ? 1 : 0;
			if (!queue->ready)
				queue->last_avail = 0;
			else if (slot->device_id == VIRTIO_ID_CONSOLE &&
				 slot->queue_sel == 0)
				orlix_virtio_mmio_start_console_input_timer(slot);
		}
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		orlix_virtio_mmio_schedule_queue(slot, value);
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		if (queue)
			queue->desc = (queue->desc & 0xffffffff00000000ULL) | value;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		if (queue)
			queue->desc = ((u64)value << 32) | (queue->desc & 0xffffffffULL);
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		if (queue)
			queue->avail = (queue->avail & 0xffffffff00000000ULL) | value;
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		if (queue)
			queue->avail = ((u64)value << 32) | (queue->avail & 0xffffffffULL);
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		if (queue)
			queue->used = (queue->used & 0xffffffff00000000ULL) | value;
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		if (queue)
			queue->used = ((u64)value << 32) | (queue->used & 0xffffffffULL);
		break;
	case VIRTIO_MMIO_SHM_SEL:
		slot->shm_sel = value;
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		slot->interrupt_status &= ~value;
		break;
	case VIRTIO_MMIO_STATUS:
		if (value == 0) {
			if (slot->notify_work_initialized)
				cancel_work_sync(&slot->notify_work);
			slot->pending_queues = 0;
			memset(slot->queues, 0, sizeof(slot->queues));
			memset(slot->driver_features, 0, sizeof(slot->driver_features));
			slot->interrupt_status = 0;
		}
		slot->status = value & 0xff;
		break;
	default:
		break;
	}

	return true;
}

bool orlix_virtio_mmio_read8(unsigned long physical_address, u8 *value)
{
	u32 word;

	if (!orlix_virtio_mmio_read32(physical_address & ~0x3UL, &word))
		return false;

	*value = (u8)(word >> ((physical_address & 0x3UL) * 8));
	return true;
}

bool orlix_virtio_mmio_read16(unsigned long physical_address, u16 *value)
{
	u32 word;

	if (!orlix_virtio_mmio_read32(physical_address & ~0x3UL, &word))
		return false;

	*value = (u16)(word >> ((physical_address & 0x2UL) * 8));
	return true;
}

bool orlix_virtio_mmio_write8(unsigned long physical_address, u8 value)
{
	u32 word;
	unsigned long aligned = physical_address & ~0x3UL;
	unsigned int shift = (physical_address & 0x3UL) * 8;

	if (!orlix_virtio_mmio_read32(aligned, &word))
		return false;

	word &= ~(0xffU << shift);
	word |= (u32)value << shift;
	return orlix_virtio_mmio_write32(aligned, word);
}

bool orlix_virtio_mmio_write16(unsigned long physical_address, u16 value)
{
	u32 word;
	unsigned long aligned = physical_address & ~0x3UL;
	unsigned int shift = (physical_address & 0x2UL) * 8;

	if (!orlix_virtio_mmio_read32(aligned, &word))
		return false;

	word &= ~(0xffffU << shift);
	word |= (u32)value << shift;
	return orlix_virtio_mmio_write32(aligned, word);
}
