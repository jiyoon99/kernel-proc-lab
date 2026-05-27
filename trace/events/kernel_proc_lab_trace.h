#undef TRACE_SYSTEM
#define TRACE_SYSTEM kernel_proc_lab

#if !defined(_TRACE_KERNEL_PROC_LAB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KERNEL_PROC_LAB_H

#include <linux/tracepoint.h>

TRACE_EVENT(kernel_proc_lab_event,
	TP_PROTO(const char *action, u64 seq, const char *message),
	TP_ARGS(action, seq, message),
	TP_STRUCT__entry(
		__string(action, action)
		__field(u64, seq)
		__string(message, message)
	),
	TP_fast_assign(
		__assign_str(action);
		__entry->seq = seq;
		__assign_str(message);
	),
	TP_printk("action=%s seq=%llu message=%s", __get_str(action),
		  __entry->seq, __get_str(message))
);

TRACE_EVENT(kernel_proc_lab_config,
	TP_PROTO(unsigned int heartbeat_interval_ms, int heartbeat_enabled),
	TP_ARGS(heartbeat_interval_ms, heartbeat_enabled),
	TP_STRUCT__entry(
		__field(unsigned int, heartbeat_interval_ms)
		__field(int, heartbeat_enabled)
	),
	TP_fast_assign(
		__entry->heartbeat_interval_ms = heartbeat_interval_ms;
		__entry->heartbeat_enabled = heartbeat_enabled;
	),
	TP_printk("heartbeat_interval_ms=%u heartbeat_enabled=%d",
		  __entry->heartbeat_interval_ms, __entry->heartbeat_enabled)
);

#endif /* _TRACE_KERNEL_PROC_LAB_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/events
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE kernel_proc_lab_trace
#include <trace/define_trace.h>
