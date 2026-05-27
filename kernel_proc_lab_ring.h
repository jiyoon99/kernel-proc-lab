#ifndef KERNEL_PROC_LAB_RING_H
#define KERNEL_PROC_LAB_RING_H

#include <linux/types.h>

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <stdbool.h>
#include <string.h>

typedef __u32 u32;
typedef __u64 u64;

static inline void kernel_proc_lab_strscpy(char *dest, const char *src,
					   size_t size)
{
	size_t len;

	if (size == 0)
		return;

	len = strnlen(src, size - 1);
	memcpy(dest, src, len);
	dest[len] = '\0';
}
#define strscpy(dest, src, size) kernel_proc_lab_strscpy(dest, src, size)
#endif

#include "kernel_proc_lab_ioctl.h"

static inline u32 kernel_proc_lab_ring_index(u64 sequence, u32 capacity)
{
	return (u32)((sequence - 1) % capacity);
}

static inline u64 kernel_proc_lab_ring_retained_start(u64 latest_sequence,
						      u32 count)
{
	if (count == 0)
		return latest_sequence + 1;

	return latest_sequence - count + 1;
}

static inline void kernel_proc_lab_ring_snapshot(
	const struct kernel_proc_lab_log_entry *entries, u32 capacity, u32 count,
	u64 latest_sequence, struct kernel_proc_lab_log_snapshot *snapshot)
{
	u64 first_sequence;
	u32 index;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->count = count;

	if (count == 0)
		return;

	first_sequence = kernel_proc_lab_ring_retained_start(latest_sequence, count);
	for (index = 0; index < count; index += 1) {
		u64 sequence = first_sequence + index;
		u32 source = kernel_proc_lab_ring_index(sequence, capacity);

		snapshot->entries[index] = entries[source];
	}
}

static inline bool kernel_proc_lab_ring_copy_entry(
	const struct kernel_proc_lab_log_entry *entries, u32 capacity, u32 count,
	u64 latest_sequence, u64 sequence,
	struct kernel_proc_lab_log_entry *entry)
{
	u64 first_sequence;
	u32 source_index;

	if (count == 0)
		return false;

	first_sequence = kernel_proc_lab_ring_retained_start(latest_sequence, count);
	if (sequence < first_sequence || sequence > latest_sequence)
		return false;

	source_index = kernel_proc_lab_ring_index(sequence, capacity);
	if (entries[source_index].seq != sequence)
		return false;

	*entry = entries[source_index];
	return true;
}

static inline void kernel_proc_lab_ring_fill_entry(
	struct kernel_proc_lab_log_entry *entries, u32 capacity, u64 sequence,
	u64 timestamp_ns, u32 type, u32 pid, u32 uid, const char *comm,
	const char *message)
{
	u32 index = kernel_proc_lab_ring_index(sequence, capacity);

	entries[index].seq = sequence;
	entries[index].timestamp_ns = timestamp_ns;
	entries[index].type = type;
	entries[index].pid = pid;
	entries[index].uid = uid;
	entries[index].reserved = 0;
	strscpy(entries[index].comm, comm, KERNEL_PROC_LAB_COMM_MAX);
	strscpy(entries[index].message, message, KERNEL_PROC_LAB_MESSAGE_MAX);
}

static inline void kernel_proc_lab_ring_fill_message(
	struct kernel_proc_lab_log_entry *entries, u32 capacity, u64 sequence,
	const char *message)
{
	kernel_proc_lab_ring_fill_entry(entries, capacity, sequence, 0,
					KERNEL_PROC_LAB_EVENT_MESSAGE, 0, 0,
					"", message);
}

#endif
