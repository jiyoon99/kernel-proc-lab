#include <stddef.h>
#include <stdio.h>

#include "../kernel_proc_lab_ioctl.h"

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

int main(void)
{
	EXPECT_EQ(KERNEL_PROC_LAB_ABI_VERSION, 4);
	EXPECT_EQ(KERNEL_PROC_LAB_MESSAGE_MAX, 128);
	EXPECT_EQ(KERNEL_PROC_LAB_COMM_MAX, 16);
	EXPECT_EQ(KERNEL_PROC_LAB_LOG_CAPACITY, 64);
	EXPECT_EQ(KERNEL_PROC_LAB_USER_LOG_CAPACITY, 64);

	EXPECT_EQ(offsetof(struct kernel_proc_lab_log_entry, seq), 0);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_log_entry, timestamp_ns), 8);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_log_entry, type), 16);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_log_entry, pid), 20);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_log_entry, uid), 24);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_log_entry, reserved), 28);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_log_entry, comm), 32);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_log_entry, message), 48);
	EXPECT_EQ(sizeof(struct kernel_proc_lab_log_entry), 176);

	EXPECT_EQ(offsetof(struct kernel_proc_lab_stats, reads), 0);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_stats, abi_version), 56);
	EXPECT_EQ(sizeof(struct kernel_proc_lab_stats), 72);

	EXPECT_EQ(offsetof(struct kernel_proc_lab_filter, enabled), 0);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_filter, type_mask), 4);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_filter, pid), 8);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_filter, uid), 12);
	EXPECT_EQ(offsetof(struct kernel_proc_lab_filter, comm), 16);
	EXPECT_EQ(sizeof(struct kernel_proc_lab_filter), 32);

	puts("ABI host tests passed");
	return 0;
}
