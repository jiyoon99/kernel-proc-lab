# JSON Output Contracts

The CLI JSON output is intentionally compact and line-oriented.

## `usercli stats --json`

```json
{
  "reads": 0,
  "writes": 0,
  "opens": 0,
  "log_events": 0,
  "heartbeat_ticks": 0,
  "dropped_log_events": 0,
  "heartbeat_enabled": 0,
  "heartbeat_interval_ms": 5000,
  "abi_version": 4,
  "log_capacity": 64,
  "message_max": 128
}
```

## `usercli log --json`

```json
{
  "count": 1,
  "entries": [
    {
      "seq": 1,
      "timestamp_ns": 123456789,
      "type": 1,
      "type_name": "write",
      "pid": 1234,
      "uid": 1000,
      "comm": "usercli",
      "message": "hello"
    }
  ]
}
```

Consumers should key behavior off `type`, not `type_name`.

## `usercli stream --json`

Stream mode emits one JSON object per event line:

```json
{
  "seq": 1,
  "timestamp_ns": 123456789,
  "type": 1,
  "type_name": "write",
  "pid": 1234,
  "uid": 1000,
  "comm": "usercli",
  "message": "hello"
}
```

Older ABI 2 streams may only include `seq` and `message`; consumers should tolerate missing typed fields during upgrades.
