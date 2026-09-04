# NTZero v4.0 ~ v5.0 development history

This file records earlier internal NTZero milestones. It is historical context, not a claim that every item is already wired into the public Alpha 0.1 source tree.

## v4.0
- scheduler wait object / deadline / wake status
- WAITING thread excluded from scheduler candidates
- `NtDelayExecution`
- Event wait moved from spin wait to blocking wait
- timer-driven timeout wake

## v4.1
- Semaphore wait/release
- Mutant acquire/release
- owner TID handoff
- dispatcher handle type checking

## v4.2
- WaitAny path for up to four Events
- timeout support

## v4.3
- IRP Pending flag
- Completion Event
- asynchronous completion core

## v4.4
- IO_CSQ
- cancel-safe IRP queue
- `IoCancelIrp`

## v4.5
- `\\Device\\NamedPipe`
- named pipe object
- handle-based pipe read/write

## v4.6
- `\\RPC Control`
- named LPC-style ports
- local message queue

## v4.7
- Job Object
- process assignment
- Active Process Limit

## v4.8
- SYSTEM hive `Services\\NTZeroSvc`
- registry-backed service database
- service state table

## v4.9
- `services.exe` Native PE
- `ntzsvc.exe` Native PE
- services.exe PID 32
- NTZeroSvc PID 36
- demand-start service path

## v5.0
- `\\RPC Control\\NTZeroServicePort`
- services.exe creates service-control port
- ntzsvc.exe opens port and sends heartbeat
- services.exe receives heartbeat
- Native System Services milestone
