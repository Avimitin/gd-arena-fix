option casemap:none

.code
PUBLIC gd_note_position_fixture
PUBLIC gd_note_position_return

; RCX points at the C++ NotePositionCall layout in note_rendering.h. Keeping
; the call instruction here gives the wrapper a stable, profile-whitelisted
; return address while still letting each test choose the callee and args.
gd_note_position_fixture PROC FRAME
    push rbx
    .pushreg rbx
    sub rsp, 40h
    .allocstack 40h
    .endprolog
    mov rbx, rcx
    mov r10, qword ptr [rbx]
    mov ecx, dword ptr [rbx+8]
    mov edx, dword ptr [rbx+12]
    movss xmm2, dword ptr [rbx+16]
    movss xmm3, dword ptr [rbx+20]
    mov eax, dword ptr [rbx+24]
    mov dword ptr [rsp+20h], eax
    mov eax, dword ptr [rbx+28]
    mov qword ptr [rsp+28h], rax
    mov eax, dword ptr [rbx+32]
    mov qword ptr [rsp+30h], rax
    movzx eax, byte ptr [rbx+36]
    mov qword ptr [rsp+38h], rax
    call r10
gd_note_position_return LABEL BYTE
    add rsp, 40h
    pop rbx
    ret
gd_note_position_fixture ENDP
END
