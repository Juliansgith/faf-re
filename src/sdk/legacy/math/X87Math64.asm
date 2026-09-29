; x87 transcendental instructions for 64-bit code (legacy/math/X87Math.cpp).
;
; MSVC has no inline assembly on x64, but the x87 unit works in long mode and
; gives the same bits as the x86 build's inline sequences. Each routine is a
; leaf: the caller's 32-byte home area at [rsp+8] is the scratch slot, so rsp
; never moves and no unwind data is needed. Arguments and results travel in
; xmm0/xmm1 (Windows x64 calling convention).

.code

; float msvc8_x87_sinf(float x)
msvc8_x87_sinf PROC
    movss   dword ptr [rsp+8], xmm0
    fld     dword ptr [rsp+8]
    fsin
    fstp    dword ptr [rsp+8]
    movss   xmm0, dword ptr [rsp+8]
    ret
msvc8_x87_sinf ENDP

; float msvc8_x87_cosf(float x)
msvc8_x87_cosf PROC
    movss   dword ptr [rsp+8], xmm0
    fld     dword ptr [rsp+8]
    fcos
    fstp    dword ptr [rsp+8]
    movss   xmm0, dword ptr [rsp+8]
    ret
msvc8_x87_cosf ENDP

; float msvc8_x87_tanf(float x)
msvc8_x87_tanf PROC
    movss   dword ptr [rsp+8], xmm0
    fld     dword ptr [rsp+8]
    fptan                               ; tan(x), then 1.0
    fstp    st(0)                       ; drop the 1.0
    fstp    dword ptr [rsp+8]
    movss   xmm0, dword ptr [rsp+8]
    ret
msvc8_x87_tanf ENDP

; float msvc8_x87_atan2f(float y, float x)
msvc8_x87_atan2f PROC
    movss   dword ptr [rsp+8], xmm0     ; y
    movss   dword ptr [rsp+16], xmm1    ; x
    fld     dword ptr [rsp+8]
    fld     dword ptr [rsp+16]
    fpatan                              ; atan(y / x) with the quadrant of (x, y)
    fstp    dword ptr [rsp+8]
    movss   xmm0, dword ptr [rsp+8]
    ret
msvc8_x87_atan2f ENDP

; float msvc8_x87_sin_times_cos(float a, float b): fsin(a) * fcos(b) on the
; unrounded results, rounded once to single (precision control forced to 24 bits
; for the multiply; x64 threads run the x87 unit at 53).
msvc8_x87_sin_times_cos PROC
    movss   dword ptr [rsp+8], xmm0
    movss   dword ptr [rsp+16], xmm1
    fnstcw  word ptr [rsp+24]
    mov     ax, word ptr [rsp+24]
    and     ax, 0FCFFh
    mov     word ptr [rsp+26], ax
    fldcw   word ptr [rsp+26]
    fld     dword ptr [rsp+8]
    fsin
    fld     dword ptr [rsp+16]
    fcos
    fmulp   st(1), st(0)
    fstp    dword ptr [rsp+8]
    fldcw   word ptr [rsp+24]
    movss   xmm0, dword ptr [rsp+8]
    ret
msvc8_x87_sin_times_cos ENDP

; float msvc8_x87_cos_times_cos(float a, float b): fcos(a) * fcos(b), as above.
msvc8_x87_cos_times_cos PROC
    movss   dword ptr [rsp+8], xmm0
    movss   dword ptr [rsp+16], xmm1
    fnstcw  word ptr [rsp+24]
    mov     ax, word ptr [rsp+24]
    and     ax, 0FCFFh
    mov     word ptr [rsp+26], ax
    fldcw   word ptr [rsp+26]
    fld     dword ptr [rsp+8]
    fcos
    fld     dword ptr [rsp+16]
    fcos
    fmulp   st(1), st(0)
    fstp    dword ptr [rsp+8]
    fldcw   word ptr [rsp+24]
    movss   xmm0, dword ptr [rsp+8]
    ret
msvc8_x87_cos_times_cos ENDP

END
