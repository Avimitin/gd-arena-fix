; Synthetic caller for inline-hook ABI and unwind tests. No game code.
PUBLIC gd_inline_probe
PUBLIC gd_inline_clobber
PUBLIC gd_cmp_flags
PUBLIC gd_number_probe
PUBLIC gd_vs_constant_fixture
PUBLIC gd_vs_constant_fixture_return
.code
; Call an isolated arithmetic block. Supply its nonstandard register inputs
; and scratch stack while preserving the host's nonvolatile registers.
gd_number_probe PROC FRAME
    push rbx
    .pushreg rbx
    push rdi
    .pushreg rdi
    push r14
    .pushreg r14
    sub rsp, 100h
    .allocstack 100h
    .endprolog
    mov [rsp+20h], r9
    mov rbx, rcx
    mov edi, edx
    call r8
    mov rcx, [rsp+20h]
    mov [rcx], r14d
    mov [rcx+4], edi
    add rsp, 100h
    pop r14
    pop rdi
    pop rbx
    ret
gd_number_probe ENDP
gd_cmp_flags PROC
    cmp ecx, edx
    pushfq
    pop rax
    ret
gd_cmp_flags ENDP
gd_inline_probe PROC FRAME
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 248h
    .allocstack 248h
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    vmovdqu ymmword ptr [rsp+040h], ymm6
    .savexmm128 xmm6, 040h
    vmovdqu ymmword ptr [rsp+060h], ymm7
    .savexmm128 xmm7, 060h
    vmovdqu ymmword ptr [rsp+080h], ymm8
    .savexmm128 xmm8, 080h
    vmovdqu ymmword ptr [rsp+0a0h], ymm9
    .savexmm128 xmm9, 0a0h
    vmovdqu ymmword ptr [rsp+0c0h], ymm10
    .savexmm128 xmm10, 0c0h
    vmovdqu ymmword ptr [rsp+0e0h], ymm11
    .savexmm128 xmm11, 0e0h
    vmovdqu ymmword ptr [rsp+0100h], ymm12
    .savexmm128 xmm12, 0100h
    vmovdqu ymmword ptr [rsp+0120h], ymm13
    .savexmm128 xmm13, 0120h
    vmovdqu ymmword ptr [rsp+0140h], ymm14
    .savexmm128 xmm14, 0140h
    vmovdqu ymmword ptr [rsp+0160h], ymm15
    .savexmm128 xmm15, 0160h
    .endprolog
    vmovdqu ymm0, ymmword ptr [r8+00h]
    vmovdqu ymm1, ymmword ptr [r8+020h]
    vmovdqu ymm2, ymmword ptr [r8+040h]
    vmovdqu ymm3, ymmword ptr [r8+060h]
    vmovdqu ymm4, ymmword ptr [r8+080h]
    vmovdqu ymm5, ymmword ptr [r8+0a0h]
    vmovdqu ymm6, ymmword ptr [r8+0c0h]
    vmovdqu ymm7, ymmword ptr [r8+0e0h]
    vmovdqu ymm8, ymmword ptr [r8+0100h]
    vmovdqu ymm9, ymmword ptr [r8+0120h]
    vmovdqu ymm10, ymmword ptr [r8+0140h]
    vmovdqu ymm11, ymmword ptr [r8+0160h]
    vmovdqu ymm12, ymmword ptr [r8+0180h]
    vmovdqu ymm13, ymmword ptr [r8+01a0h]
    vmovdqu ymm14, ymmword ptr [r8+01c0h]
    vmovdqu ymm15, ymmword ptr [r8+01e0h]
    mov rax, 0100h
    mov rcx, 0101h
    mov rdx, 0102h
    mov rbx, 0103h
    mov rsi, 0104h
    mov rdi, 0105h
    mov r8, 0106h
    mov r9, 0107h
    mov r10, 0108h
    mov r11, 0109h
    mov r12, 010ah
    mov r13, 010bh
    mov r14, 010ch
    mov r15, 010dh
    mov rbp, 010eh
    call qword ptr [rsp+20h]
    push r11
    mov r11, [rsp+30h]
    mov [r11+00h], rax
    mov [r11+08h], rcx
    mov [r11+010h], rdx
    mov [r11+018h], rbx
    mov [r11+020h], rsi
    mov [r11+028h], rdi
    mov [r11+030h], r8
    mov [r11+038h], r9
    mov [r11+040h], r10
    mov [r11+050h], r12
    mov [r11+058h], r13
    mov [r11+060h], r14
    mov [r11+068h], r15
    mov [r11+070h], rbp
    mov rax, [rsp]
    mov [r11+48h], rax
    pushfq
    pop qword ptr [r11+78h]
    vmovdqu ymmword ptr [r11+0100h], ymm0
    vmovdqu ymmword ptr [r11+0120h], ymm1
    vmovdqu ymmword ptr [r11+0140h], ymm2
    vmovdqu ymmword ptr [r11+0160h], ymm3
    vmovdqu ymmword ptr [r11+0180h], ymm4
    vmovdqu ymmword ptr [r11+01a0h], ymm5
    vmovdqu ymmword ptr [r11+01c0h], ymm6
    vmovdqu ymmword ptr [r11+01e0h], ymm7
    vmovdqu ymmword ptr [r11+0200h], ymm8
    vmovdqu ymmword ptr [r11+0220h], ymm9
    vmovdqu ymmword ptr [r11+0240h], ymm10
    vmovdqu ymmword ptr [r11+0260h], ymm11
    vmovdqu ymmword ptr [r11+0280h], ymm12
    vmovdqu ymmword ptr [r11+02a0h], ymm13
    vmovdqu ymmword ptr [r11+02c0h], ymm14
    vmovdqu ymmword ptr [r11+02e0h], ymm15
    add rsp, 8
    vmovdqu ymm6, ymmword ptr [rsp+040h]
    vmovdqu ymm7, ymmword ptr [rsp+060h]
    vmovdqu ymm8, ymmword ptr [rsp+080h]
    vmovdqu ymm9, ymmword ptr [rsp+0a0h]
    vmovdqu ymm10, ymmword ptr [rsp+0c0h]
    vmovdqu ymm11, ymmword ptr [rsp+0e0h]
    vmovdqu ymm12, ymmword ptr [rsp+0100h]
    vmovdqu ymm13, ymmword ptr [rsp+0120h]
    vmovdqu ymm14, ymmword ptr [rsp+0140h]
    vmovdqu ymm15, ymmword ptr [rsp+0160h]
    vzeroupper
    add rsp, 248h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
gd_inline_probe ENDP
gd_inline_clobber PROC
    xor rax, rax
    xor rcx, rcx
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    vxorps ymm0, ymm0, ymm0
    vxorps ymm1, ymm1, ymm1
    vxorps ymm2, ymm2, ymm2
    vxorps ymm3, ymm3, ymm3
    vxorps ymm4, ymm4, ymm4
    vxorps ymm5, ymm5, ymm5
    vzeroupper
    ret
gd_inline_clobber ENDP
gd_vs_constant_fixture PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    mov rax, rcx
    mov rcx, rdx
    mov rdx, r8
    mov r8, r9
    mov r9d, dword ptr [rsp+50h]
    call rax
gd_vs_constant_fixture_return LABEL BYTE
    add rsp, 28h
    ret
gd_vs_constant_fixture ENDP
END
