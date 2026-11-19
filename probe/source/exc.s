// SW2PS2: user exception entry that can resume. The kernel enters with
// x0 = error description, x1 = exception frame (x0-x8, lr, sp, pc, pstate, ...).
// Registers outside the frame must be exactly as they were when the handler
// returns through svcReturnFromException(0), which retries the instruction.
.text
.global __libnx_exception_entry
.type __libnx_exception_entry, %function
__libnx_exception_entry:
    sub sp, sp, #0x2c0
    stp x9, x10, [sp, #0x00]
    stp x11, x12, [sp, #0x10]
    stp x13, x14, [sp, #0x20]
    stp x15, x16, [sp, #0x30]
    stp x17, x18, [sp, #0x40]
    stp x19, x20, [sp, #0x50]
    stp x21, x22, [sp, #0x60]
    stp x23, x24, [sp, #0x70]
    stp x25, x26, [sp, #0x80]
    stp x27, x28, [sp, #0x90]
    stp x29, x30, [sp, #0xa0]
    mrs x9, fpcr
    mrs x10, fpsr
    stp x9, x10, [sp, #0xb0]
    add x9, sp, #0xc0
    stp q0, q1, [x9, #0x000]
    stp q2, q3, [x9, #0x020]
    stp q4, q5, [x9, #0x040]
    stp q6, q7, [x9, #0x060]
    stp q8, q9, [x9, #0x080]
    stp q10, q11, [x9, #0x0a0]
    stp q12, q13, [x9, #0x0c0]
    stp q14, q15, [x9, #0x0e0]
    stp q16, q17, [x9, #0x100]
    stp q18, q19, [x9, #0x120]
    stp q20, q21, [x9, #0x140]
    stp q22, q23, [x9, #0x160]
    stp q24, q25, [x9, #0x180]
    stp q26, q27, [x9, #0x1a0]
    stp q28, q29, [x9, #0x1c0]
    stp q30, q31, [x9, #0x1e0]
    bl sw2_exception
    mov w8, w0
    add x9, sp, #0xc0
    ldp q0, q1, [x9, #0x000]
    ldp q2, q3, [x9, #0x020]
    ldp q4, q5, [x9, #0x040]
    ldp q6, q7, [x9, #0x060]
    ldp q8, q9, [x9, #0x080]
    ldp q10, q11, [x9, #0x0a0]
    ldp q12, q13, [x9, #0x0c0]
    ldp q14, q15, [x9, #0x0e0]
    ldp q16, q17, [x9, #0x100]
    ldp q18, q19, [x9, #0x120]
    ldp q20, q21, [x9, #0x140]
    ldp q22, q23, [x9, #0x160]
    ldp q24, q25, [x9, #0x180]
    ldp q26, q27, [x9, #0x1a0]
    ldp q28, q29, [x9, #0x1c0]
    ldp q30, q31, [x9, #0x1e0]
    ldp x9, x10, [sp, #0xb0]
    msr fpcr, x9
    msr fpsr, x10
    ldp x9, x10, [sp, #0x00]
    ldp x11, x12, [sp, #0x10]
    ldp x13, x14, [sp, #0x20]
    ldp x15, x16, [sp, #0x30]
    ldp x17, x18, [sp, #0x40]
    ldp x19, x20, [sp, #0x50]
    ldp x21, x22, [sp, #0x60]
    ldp x23, x24, [sp, #0x70]
    ldp x25, x26, [sp, #0x80]
    ldp x27, x28, [sp, #0x90]
    ldp x29, x30, [sp, #0xa0]
    add sp, sp, #0x2c0
    cbz w8, 1f
    mov w0, wzr
    svc 0x28          // svcReturnFromException(0): resume
1:  mov w0, #0xf801   // not ours: let the system report the crash
    svc 0x28
    b .
