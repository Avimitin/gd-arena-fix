; The original sprite updater preserves R10/R11 and all XMM registers. Its
; optimized callers keep their loop pointer in R10 across the call, despite
; R10 being volatile in the public Windows ABI. Preserve that private ABI.
EXTERN gd_sprite_update:PROC
PUBLIC gd_sprite_shim
.code
gd_sprite_shim PROC FRAME
    push r10
    .allocstack 8
    push r11
    .allocstack 8
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    movdqu [rsp+020h], xmm0
    movdqu [rsp+030h], xmm1
    movdqu [rsp+040h], xmm2
    movdqu [rsp+050h], xmm3
    movdqu [rsp+060h], xmm4
    movdqu [rsp+070h], xmm5
    call gd_sprite_update
    movdqu xmm0, [rsp+020h]
    movdqu xmm1, [rsp+030h]
    movdqu xmm2, [rsp+040h]
    movdqu xmm3, [rsp+050h]
    movdqu xmm4, [rsp+060h]
    movdqu xmm5, [rsp+070h]
    add rsp, 088h
    pop r11
    pop r10
    ret
gd_sprite_shim ENDP
END
