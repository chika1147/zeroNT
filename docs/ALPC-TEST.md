# NTZero Alpha 0.1 test notes

The public ALPC transport can be checked on a normal host compiler with `tests/alpc_host_test.c` plus `kernel/alpc.c` and `include/`.

Validated path:

```text
Create named connection port
  -> client Connect
  -> server Accept
  -> client sends "heartbeat"
  -> server receives the message
  -> sender PID and payload verified
```

The artifact-build run printed:

```text
ALPC PASS id=1 sender=36
```
