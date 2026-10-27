; Preserve every GPR, flags, all YMM registers and MXCSR at a native instruction.
; A fixed frame pointer permits either native stack alignment and unwind.
EXTERN gd_inline_dispatch:PROC
PUBLIC gd_inline_shim
.code
gd_inline_shim PROC FRAME
    push rbp
    .pushreg rbp
    pushfq
    .allocstack 8
    sub rsp, 2b0h
    .allocstack 2b0h
    mov rbp, rsp
    .setframe rbp, 0
    mov [rbp+240h], r15
    .savereg r15, 240h
    mov [rbp+248h], r14
    .savereg r14, 248h
    mov [rbp+250h], r13
    .savereg r13, 250h
    mov [rbp+258h], r12
    .savereg r12, 258h
    mov [rbp+260h], r11
    mov [rbp+268h], r10
    mov [rbp+270h], r9
    mov [rbp+278h], r8
    mov [rbp+280h], rdi
    .savereg rdi, 280h
    mov [rbp+288h], rsi
    .savereg rsi, 288h
    mov [rbp+290h], rbx
    .savereg rbx, 290h
    mov [rbp+298h], rdx
    mov [rbp+2a0h], rcx
    mov [rbp+2a8h], rax
    vmovdqu ymmword ptr [rbp+0100h], ymm6
    .savexmm128 xmm6, 0100h
    vmovdqu ymmword ptr [rbp+0120h], ymm7
    .savexmm128 xmm7, 0120h
    vmovdqu ymmword ptr [rbp+0140h], ymm8
    .savexmm128 xmm8, 0140h
    vmovdqu ymmword ptr [rbp+0160h], ymm9
    .savexmm128 xmm9, 0160h
    vmovdqu ymmword ptr [rbp+0180h], ymm10
    .savexmm128 xmm10, 0180h
    vmovdqu ymmword ptr [rbp+01a0h], ymm11
    .savexmm128 xmm11, 01a0h
    vmovdqu ymmword ptr [rbp+01c0h], ymm12
    .savexmm128 xmm12, 01c0h
    vmovdqu ymmword ptr [rbp+01e0h], ymm13
    .savexmm128 xmm13, 01e0h
    vmovdqu ymmword ptr [rbp+0200h], ymm14
    .savexmm128 xmm14, 0200h
    vmovdqu ymmword ptr [rbp+0220h], ymm15
    .savexmm128 xmm15, 0220h
    .endprolog
    stmxcsr [rbp+20h]
    vmovdqu ymmword ptr [rbp+040h], ymm0
    vmovdqu ymmword ptr [rbp+060h], ymm1
    vmovdqu ymmword ptr [rbp+080h], ymm2
    vmovdqu ymmword ptr [rbp+0a0h], ymm3
    vmovdqu ymmword ptr [rbp+0c0h], ymm4
    vmovdqu ymmword ptr [rbp+0e0h], ymm5
    and rsp, -16
    vzeroupper
    lea rcx, [rbp+240h]
    call gd_inline_dispatch
    ldmxcsr [rbp+20h]
    vmovdqu ymm0, ymmword ptr [rbp+040h]
    vmovdqu ymm1, ymmword ptr [rbp+060h]
    vmovdqu ymm2, ymmword ptr [rbp+080h]
    vmovdqu ymm3, ymmword ptr [rbp+0a0h]
    vmovdqu ymm4, ymmword ptr [rbp+0c0h]
    vmovdqu ymm5, ymmword ptr [rbp+0e0h]
    vmovdqu ymm6, ymmword ptr [rbp+0100h]
    vmovdqu ymm7, ymmword ptr [rbp+0120h]
    vmovdqu ymm8, ymmword ptr [rbp+0140h]
    vmovdqu ymm9, ymmword ptr [rbp+0160h]
    vmovdqu ymm10, ymmword ptr [rbp+0180h]
    vmovdqu ymm11, ymmword ptr [rbp+01a0h]
    vmovdqu ymm12, ymmword ptr [rbp+01c0h]
    vmovdqu ymm13, ymmword ptr [rbp+01e0h]
    vmovdqu ymm14, ymmword ptr [rbp+0200h]
    vmovdqu ymm15, ymmword ptr [rbp+0220h]
    mov r15, [rbp+240h]
    mov r14, [rbp+248h]
    mov r13, [rbp+250h]
    mov r12, [rbp+258h]
    mov r11, [rbp+260h]
    mov r10, [rbp+268h]
    mov r9, [rbp+270h]
    mov r8, [rbp+278h]
    mov rdi, [rbp+280h]
    mov rsi, [rbp+288h]
    mov rbx, [rbp+290h]
    mov rdx, [rbp+298h]
    mov rcx, [rbp+2a0h]
    mov rax, [rbp+2a8h]
    push qword ptr [rbp+2b0h]
    popfq
    lea rsp, [rbp+2b8h]
    pop rbp
    ret
gd_inline_shim ENDP
END
