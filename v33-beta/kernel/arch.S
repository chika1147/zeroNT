.section .text
.global zeront_load_gdt
.type zeront_load_gdt,@function
zeront_load_gdt:
    lgdt (%rdi)

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs

    pushq $0x08
    leaq .Lgdt_ready(%rip), %rax
    pushq %rax
    lretq
.Lgdt_ready:
    ret
.size zeront_load_gdt, .-zeront_load_gdt

.global zeront_load_cr3
.type zeront_load_cr3,@function
zeront_load_cr3:
    movq %rdi, %cr3
    ret
.size zeront_load_cr3, .-zeront_load_cr3

.global zeront_read_cr3
.type zeront_read_cr3,@function
zeront_read_cr3:
    movq %cr3, %rax
    ret
.size zeront_read_cr3, .-zeront_read_cr3

.global zeront_invlpg
.type zeront_invlpg,@function
zeront_invlpg:
    invlpg (%rdi)
    ret
.size zeront_invlpg, .-zeront_invlpg

.macro ZERONT_SAVE_GPRS
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro ZERONT_RESTORE_GPRS
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
.endm

.global zeront_irq_timer_entry
.type zeront_irq_timer_entry,@function
zeront_irq_timer_entry:
    cld
    ZERONT_SAVE_GPRS
    movq %rsp, %rdi
    andq $-16, %rsp
    callq zeront_timer_dispatch
    movq %rax, %rsp
    ZERONT_RESTORE_GPRS
    iretq
.size zeront_irq_timer_entry, .-zeront_irq_timer_entry

.global zeront_schedule_entry
.type zeront_schedule_entry,@function
zeront_schedule_entry:
    cld
    ZERONT_SAVE_GPRS
    movq %rsp, %rdi
    andq $-16, %rsp
    callq zeront_schedule_dispatch
    movq %rax, %rsp
    ZERONT_RESTORE_GPRS
    iretq
.size zeront_schedule_entry, .-zeront_schedule_entry

.global zeront_scheduler_yield_interrupt
.type zeront_scheduler_yield_interrupt,@function
zeront_scheduler_yield_interrupt:
    int $0x30
    ret
.size zeront_scheduler_yield_interrupt, .-zeront_scheduler_yield_interrupt

.section .bss
.align 8
zeront_user_kernel_rsp:
    .quad 0
zeront_user_exit_code:
    .quad 0

.section .text
.global zeront_enter_user
.type zeront_enter_user,@function
zeront_enter_user:
    # rdi=user RIP, rsi=user RSP, rdx=arg0
    movq %rsp, zeront_user_kernel_rsp(%rip)
    movq %rdx, %r8
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    pushq $0x23
    pushq %rsi
    pushq $0x2              # IF stays clear during synchronous smoke test
    pushq $0x1B
    pushq %rdi
    movq %r8, %rdi
    iretq
.size zeront_enter_user, .-zeront_enter_user

.global zeront_syscall_entry
.type zeront_syscall_entry,@function
zeront_syscall_entry:
    cld
    ZERONT_SAVE_GPRS
    movq %rsp, %rdi
    movq %rsp, %r12
    andq $-16, %rsp
    callq zeront_syscall_dispatch
    movq %r12, %rsp
    cmpq $1, %rax
    je .Lsys_exit_to_kernel
    cmpq $2, %rax
    je .Lsys_yield_to_kernel
    ZERONT_RESTORE_GPRS
    iretq
.Lsys_exit_to_kernel:
    # saved frame rdi is qword 8 from top after ZERONT_SAVE_GPRS
    movq 64(%rsp), %rax
    movq %rax, zeront_user_exit_code(%rip)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    movq zeront_user_kernel_rsp(%rip), %rsp
    movq zeront_user_exit_code(%rip), %rax
    ret
.Lsys_yield_to_kernel:
    # A cooperative Ring-3 task saved its full frame in usermode.cpp.
    # Return to the kernel caller without terminating the process.
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    movq zeront_user_kernel_rsp(%rip), %rsp
    xorq %rax, %rax
    ret
.size zeront_syscall_entry, .-zeront_syscall_entry

.global zeront_resume_user
.type zeront_resume_user,@function
zeront_resume_user:
    # rdi = const SyscallFrame*.  Re-enter Ring 3 using the exact register
    # state captured by SysYield.  The context layout matches SyscallFrame.
    movq %rsp, zeront_user_kernel_rsp(%rip)
    movq %rdi, %rsi
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es

    pushq 152(%rsi)          # ss
    pushq 144(%rsi)          # rsp
    pushq 136(%rsi)          # rflags
    pushq 128(%rsi)          # cs
    pushq 120(%rsi)          # rip

    movq   0(%rsi), %r15
    movq   8(%rsi), %r14
    movq  16(%rsi), %r13
    movq  24(%rsi), %r12
    movq  32(%rsi), %r11
    movq  40(%rsi), %r10
    movq  48(%rsi), %r9
    movq  56(%rsi), %r8
    movq  64(%rsi), %rdi
    movq  80(%rsi), %rbp
    movq  88(%rsi), %rdx
    movq  96(%rsi), %rcx
    movq 104(%rsi), %rbx
    movq 112(%rsi), %rax
    movq  72(%rsi), %rsi
    iretq
.size zeront_resume_user, .-zeront_resume_user

.global zeront_abort_user
.type zeront_abort_user,@function
zeront_abort_user:
    # rdi = negative fault/exit code. Abandon the ring-3 interrupt frame and
    # return to the synchronous zeront_enter_user caller instead of killing the kernel.
    movq %rdi, zeront_user_exit_code(%rip)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    movq zeront_user_kernel_rsp(%rip), %rsp
    movq zeront_user_exit_code(%rip), %rax
    ret
.size zeront_abort_user, .-zeront_abort_user
