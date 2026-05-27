// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the pure kernel_proc_lab ring-buffer helpers.
 *
 * This file is not wired into the external-module Makefile by default because
 * KUnit availability depends on the target kernel config. It is ready to be
 * added to a KUnit-enabled Kbuild environment.
 */

#include <kunit/test.h>

#include "../kernel_proc_lab_ring.h"

static void kernel_proc_lab_abi_constants_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, KERNEL_PROC_LAB_ABI_VERSION, 4);
	KUNIT_EXPECT_EQ(test, KERNEL_PROC_LAB_MESSAGE_MAX, 128);
	KUNIT_EXPECT_EQ(test, KERNEL_PROC_LAB_LOG_CAPACITY, 64);
	KUNIT_EXPECT_EQ(test, KERNEL_PROC_LAB_USER_LOG_CAPACITY, 64);
}

static void kernel_proc_lab_retained_start_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, kernel_proc_lab_ring_retained_start(0, 0), 1);
	KUNIT_EXPECT_EQ(test, kernel_proc_lab_ring_retained_start(4, 4), 1);
	KUNIT_EXPECT_EQ(test, kernel_proc_lab_ring_retained_start(70, 64), 7);
}

static void kernel_proc_lab_ring_snapshot_wrap_test(struct kunit *test)
{
	struct kernel_proc_lab_log_entry entries[KERNEL_PROC_LAB_LOG_CAPACITY] = {};
	struct kernel_proc_lab_log_snapshot snapshot;
	u64 sequence;
	u32 index;

	for (sequence = 1; sequence <= 70; sequence += 1) {
		char message[KERNEL_PROC_LAB_MESSAGE_MAX];

		snprintf(message, sizeof(message), "event-%llu", sequence);
		kernel_proc_lab_ring_fill_message(entries, KERNEL_PROC_LAB_LOG_CAPACITY,
						  sequence, message);
	}

	kernel_proc_lab_ring_snapshot(entries, KERNEL_PROC_LAB_LOG_CAPACITY,
				      KERNEL_PROC_LAB_LOG_CAPACITY, 70, &snapshot);

	KUNIT_EXPECT_EQ(test, snapshot.count, KERNEL_PROC_LAB_LOG_CAPACITY);
	KUNIT_EXPECT_EQ(test, snapshot.entries[0].seq, 7);
	KUNIT_EXPECT_STREQ(test, snapshot.entries[0].message, "event-7");

	index = KERNEL_PROC_LAB_LOG_CAPACITY - 1;
	KUNIT_EXPECT_EQ(test, snapshot.entries[index].seq, 70);
	KUNIT_EXPECT_STREQ(test, snapshot.entries[index].message, "event-70");
}

static void kernel_proc_lab_ring_copy_entry_test(struct kunit *test)
{
	struct kernel_proc_lab_log_entry entries[KERNEL_PROC_LAB_LOG_CAPACITY] = {};
	struct kernel_proc_lab_log_entry entry;
	u64 sequence;

	for (sequence = 1; sequence <= 70; sequence += 1) {
		char message[KERNEL_PROC_LAB_MESSAGE_MAX];

		snprintf(message, sizeof(message), "event-%llu", sequence);
		kernel_proc_lab_ring_fill_message(entries, KERNEL_PROC_LAB_LOG_CAPACITY,
						  sequence, message);
	}

	KUNIT_EXPECT_FALSE(test, kernel_proc_lab_ring_copy_entry(
					   entries, KERNEL_PROC_LAB_LOG_CAPACITY,
					   KERNEL_PROC_LAB_LOG_CAPACITY, 70, 6,
					   &entry));
	KUNIT_EXPECT_TRUE(test, kernel_proc_lab_ring_copy_entry(
					  entries, KERNEL_PROC_LAB_LOG_CAPACITY,
					  KERNEL_PROC_LAB_LOG_CAPACITY, 70, 7,
					  &entry));
	KUNIT_EXPECT_EQ(test, entry.seq, 7);
	KUNIT_EXPECT_STREQ(test, entry.message, "event-7");
}

static struct kunit_case kernel_proc_lab_test_cases[] = {
	KUNIT_CASE(kernel_proc_lab_abi_constants_test),
	KUNIT_CASE(kernel_proc_lab_retained_start_test),
	KUNIT_CASE(kernel_proc_lab_ring_snapshot_wrap_test),
	KUNIT_CASE(kernel_proc_lab_ring_copy_entry_test),
	{}
};

static struct kunit_suite kernel_proc_lab_test_suite = {
	.name = "kernel_proc_lab",
	.test_cases = kernel_proc_lab_test_cases,
};

kunit_test_suite(kernel_proc_lab_test_suite);
