#include <stdio.h>
#include <string.h>

#include "../kernel_proc_lab_ring.h"

#define EXPECT_TRUE(expr)                                                       \
	do {                                                                    \
		if (!(expr)) {                                                  \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
				#expr);                                         \
			return 1;                                               \
		}                                                               \
	} while (0)

#define EXPECT_EQ(actual, expected)                                             \
	do {                                                                    \
		unsigned long long actual_value = (unsigned long long)(actual); \
		unsigned long long expected_value =                            \
			(unsigned long long)(expected);                        \
		if (actual_value != expected_value) {                          \
			fprintf(stderr,                                      \
				"FAIL %s:%d: %s=%llu expected %llu\n",       \
				__FILE__, __LINE__, #actual, actual_value,    \
				expected_value);                              \
			return 1;                                               \
		}                                                               \
	} while (0)

#define EXPECT_STREQ(actual, expected)                                          \
	do {                                                                    \
		if (strcmp((actual), (expected)) != 0) {                         \
			fprintf(stderr,                                      \
				"FAIL %s:%d: %s=\"%s\" expected \"%s\"\n",   \
				__FILE__, __LINE__, #actual, (actual),        \
				(expected));                                  \
			return 1;                                               \
		}                                                               \
	} while (0)

static int constants_test(void)
{
	EXPECT_EQ(KERNEL_PROC_LAB_ABI_VERSION, 4);
	EXPECT_EQ(KERNEL_PROC_LAB_MESSAGE_MAX, 128);
	EXPECT_EQ(KERNEL_PROC_LAB_LOG_CAPACITY, 64);
	EXPECT_EQ(KERNEL_PROC_LAB_USER_LOG_CAPACITY, 64);
	return 0;
}

static int retained_start_test(void)
{
	EXPECT_EQ(kernel_proc_lab_ring_retained_start(0, 0), 1);
	EXPECT_EQ(kernel_proc_lab_ring_retained_start(4, 4), 1);
	EXPECT_EQ(kernel_proc_lab_ring_retained_start(70, 64), 7);
	return 0;
}

static int snapshot_wrap_test(void)
{
	struct kernel_proc_lab_log_entry entries[KERNEL_PROC_LAB_LOG_CAPACITY] = {};
	struct kernel_proc_lab_log_snapshot snapshot;
	u64 sequence;
	u32 index;

	for (sequence = 1; sequence <= 70; sequence += 1) {
		char message[KERNEL_PROC_LAB_MESSAGE_MAX];

		snprintf(message, sizeof(message), "event-%llu",
			 (unsigned long long)sequence);
		kernel_proc_lab_ring_fill_message(entries, KERNEL_PROC_LAB_LOG_CAPACITY,
						  sequence, message);
	}

	kernel_proc_lab_ring_snapshot(entries, KERNEL_PROC_LAB_LOG_CAPACITY,
				      KERNEL_PROC_LAB_LOG_CAPACITY, 70, &snapshot);

	EXPECT_EQ(snapshot.count, KERNEL_PROC_LAB_LOG_CAPACITY);
	EXPECT_EQ(snapshot.entries[0].seq, 7);
	EXPECT_STREQ(snapshot.entries[0].message, "event-7");

	index = KERNEL_PROC_LAB_LOG_CAPACITY - 1;
	EXPECT_EQ(snapshot.entries[index].seq, 70);
	EXPECT_STREQ(snapshot.entries[index].message, "event-70");
	return 0;
}

static int snapshot_clamps_to_user_capacity_test(void)
{
	struct kernel_proc_lab_log_entry entries[128] = {};
	struct kernel_proc_lab_log_snapshot snapshot;
	u64 sequence;
	u32 index;

	for (sequence = 1; sequence <= 128; sequence += 1) {
		char message[KERNEL_PROC_LAB_MESSAGE_MAX];

		snprintf(message, sizeof(message), "event-%llu",
			 (unsigned long long)sequence);
		kernel_proc_lab_ring_fill_message(entries, 128, sequence, message);
	}

	kernel_proc_lab_ring_snapshot(entries, 128, 128, 128, &snapshot);

	EXPECT_EQ(snapshot.count, KERNEL_PROC_LAB_USER_LOG_CAPACITY);
	EXPECT_EQ(snapshot.entries[0].seq, 65);
	EXPECT_STREQ(snapshot.entries[0].message, "event-65");

	index = KERNEL_PROC_LAB_USER_LOG_CAPACITY - 1;
	EXPECT_EQ(snapshot.entries[index].seq, 128);
	EXPECT_STREQ(snapshot.entries[index].message, "event-128");
	return 0;
}

static int copy_entry_test(void)
{
	struct kernel_proc_lab_log_entry entries[KERNEL_PROC_LAB_LOG_CAPACITY] = {};
	struct kernel_proc_lab_log_entry entry;
	u64 sequence;

	for (sequence = 1; sequence <= 70; sequence += 1) {
		char message[KERNEL_PROC_LAB_MESSAGE_MAX];

		snprintf(message, sizeof(message), "event-%llu",
			 (unsigned long long)sequence);
		kernel_proc_lab_ring_fill_message(entries, KERNEL_PROC_LAB_LOG_CAPACITY,
						  sequence, message);
	}

	EXPECT_TRUE(!kernel_proc_lab_ring_copy_entry(
		entries, KERNEL_PROC_LAB_LOG_CAPACITY,
		KERNEL_PROC_LAB_LOG_CAPACITY, 70, 6, &entry));
	EXPECT_TRUE(kernel_proc_lab_ring_copy_entry(
		entries, KERNEL_PROC_LAB_LOG_CAPACITY,
		KERNEL_PROC_LAB_LOG_CAPACITY, 70, 7, &entry));
	EXPECT_EQ(entry.seq, 7);
	EXPECT_STREQ(entry.message, "event-7");
	return 0;
}

static int truncation_test(void)
{
	struct kernel_proc_lab_log_entry entries[KERNEL_PROC_LAB_LOG_CAPACITY] = {};
	char message[KERNEL_PROC_LAB_MESSAGE_MAX * 2];
	size_t index;

	memset(message, 'x', sizeof(message));
	message[sizeof(message) - 1] = '\0';

	kernel_proc_lab_ring_fill_entry(entries, KERNEL_PROC_LAB_LOG_CAPACITY, 1, 12345,
					KERNEL_PROC_LAB_EVENT_WRITE, 42, 1000,
					"host-test", message);

	EXPECT_EQ(entries[0].seq, 1);
	EXPECT_EQ(entries[0].timestamp_ns, 12345);
	EXPECT_EQ(entries[0].type, KERNEL_PROC_LAB_EVENT_WRITE);
	EXPECT_EQ(entries[0].pid, 42);
	EXPECT_EQ(entries[0].uid, 1000);
	EXPECT_STREQ(entries[0].comm, "host-test");
	for (index = 0; index < KERNEL_PROC_LAB_MESSAGE_MAX - 1; index += 1)
		EXPECT_EQ(entries[0].message[index], 'x');
	EXPECT_EQ(entries[0].message[KERNEL_PROC_LAB_MESSAGE_MAX - 1], '\0');
	return 0;
}

int main(void)
{
	if (constants_test() || retained_start_test() || snapshot_wrap_test() ||
	    snapshot_clamps_to_user_capacity_test() || copy_entry_test() ||
	    truncation_test())
		return 1;

	puts("ring host tests passed");
	return 0;
}
