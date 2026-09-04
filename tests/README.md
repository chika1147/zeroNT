# Tests

Host-side tests live here so small kernel subsystems can be checked without booting the full OS.

Current Alpha 0.1 test:

- `alpc_host_test.c` — verifies named port create/connect/accept and a heartbeat message transfer.
