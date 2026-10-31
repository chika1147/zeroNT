#pragma once
#include <stdint.h>

struct SyscallFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
};

enum ZeroNTSyscall : uint64_t {
    SysExit = 1,
    SysGetVersion = 2,
    SysPing = 3,
    SysYield = 4,
    SysGuiCreateWindow = 10,
    SysGuiFillRect = 11,
    SysGuiGetEvent = 12,
    SysGuiCloseWindow = 13,
    SysGuiSetTitle = 14,
    SysGuiDrawText = 15,
    SysGuiBeginPaint = 16,
    SysGuiScreenSize = 17,
    SysDisplayCaps = 18,
    SysDisplaySetMode = 19,
    SysAudioTone = 20,
    SysAudioSetVolume = 21,
    SysAudioSubmitPcm = 22,
    SysAudioStop = 23,
    SysAudioStatus = 24,
    SysAudioMute = 25,
    SysNetworkPingGateway = 30,
    SysNetworkStatus = 31,
    SysNetworkDnsLookup = 32,
    SysNetworkTcpProbe = 33,
    SysNetworkTcpConnect = 34,
    SysNetworkTcpSend = 35,
    SysNetworkTcpReceive = 36,
    SysNetworkTcpClose = 37,
    SysNetworkUdpOpen = 70,
    SysNetworkUdpSend = 71,
    SysNetworkUdpReceive = 72,
    SysNetworkUdpClose = 73,
    SysNetworkDhcpRestart = 74,
    SysUserInfo = 40,
    SysUserPermissions = 41,
    SysUserLogout = 42,
    SysProcessId = 50,
    SysShellOpenApp = 60,
};

void syscall_init();
extern "C" uint64_t zeront_syscall_dispatch(SyscallFrame* frame);
uint64_t syscall_count();
uint64_t syscall_ping_count();
int64_t syscall_last_exit_code();
