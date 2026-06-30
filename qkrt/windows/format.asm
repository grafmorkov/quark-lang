; common/format.asm
; Numeric-to-string conversion routines
;
; All return pointer to null-terminated string in RAX.
; Each function owns a static buffer.

section '.text' code readable executable

; ============================================================
; qk_format_i64(rax: int64) -> string
; ============================================================
qk_format_i64:
    push rbx
    push rdi

    lea rdi, [.buf + 31]
    mov byte [rdi], 0
    dec rdi

    mov rbx, rax
    test rax, rax
    jns .positive
    neg rax

.positive:
    mov rcx, 10
    test rax, rax
    jnz .loop
    mov byte [rdi], '0'
    dec rdi
    jmp .sign

.loop:
    xor rdx, rdx
    div rcx
    add dl, '0'
    mov [rdi], dl
    dec rdi
    test rax, rax
    jnz .loop

.sign:
    test rbx, rbx
    jns .done
    mov byte [rdi], '-'
    dec rdi

.done:
    inc rdi
    mov rax, rdi
    pop rdi
    pop rbx
    ret

section '.data' data readable writeable
.buf: rb 32
align 8
section '.text' code readable executable

; ============================================================
; qk_format_u64(rax: uint64) -> string
; ============================================================
qk_format_u64:
    push rdi

    lea rdi, [.buf + 31]
    mov byte [rdi], 0
    dec rdi

    mov rcx, 10
    test rax, rax
    jnz .loop
    mov byte [rdi], '0'
    dec rdi
    jmp .done

.loop:
    xor rdx, rdx
    div rcx
    add dl, '0'
    mov [rdi], dl
    dec rdi
    test rax, rax
    jnz .loop

.done:
    inc rdi
    mov rax, rdi
    pop rdi
    ret

section '.data' data readable writeable
.buf: rb 32
align 8
section '.text' code readable executable

; ============================================================
; qk_format_f64(xmm0: double) -> string
;
; Format: [-]digits.fraction (up to 6 fraction digits)
; Special: "nan", "inf", "-inf"
; Clobbers: xmm0, xmm1
; ============================================================
qk_format_f64:
    push rbx
    push rdi
    push rsi
    sub rsp, 40

    movsd qword [rsp], xmm0

    lea rdi, [.buf + 63]
    mov byte [rdi], 0
    dec rdi

    ; check NaN/Inf
    mov rax, qword [rsp]
    mov rbx, rax
    shr rbx, 63
    mov rcx, rax
    shr rcx, 52
    and ecx, 0x7FF
    ; check mantissa (52 bits) using two operations for FASM
    mov rdx, rax
    shl rdx, 12          ; shift left to discard sign+exponent (12 = 64-52)
    shr rdx, 12          ; shift right to zero-extend mantissa

    cmp ecx, 0x7FF
    jne .normal

    test rdx, rdx
    jnz .is_nan
    test rbx, rbx
    jnz .is_neg_inf
    lea rax, [.str_inf]
    jmp .ret
.is_nan:
    lea rax, [.str_nan]
    jmp .ret
.is_neg_inf:
    lea rax, [.str_neg_inf]
    jmp .ret

    ; normal finite number
.normal:
    movsd xmm0, qword [rsp]
    test rbx, rbx
    jz .abs
    mov byte [rdi], '-'
    dec rdi
    mov rax, 0x7FFFFFFFFFFFFFFF
    movq xmm1, rax
    andpd xmm0, xmm1

.abs:
    ; check if xmm0 is within int64 range: [-2^63, 2^63-1]
    mov rax, 0x43E0000000000000    ; double 2^63
    movq xmm1, rax
    comisd xmm0, xmm1
    jae .overflow                  ; xmm0 >= 2^63 -> overflow
    mov rax, 0xC3E0000000000000    ; double -2^63
    movq xmm1, rax
    comisd xmm0, xmm1
    jb .overflow                   ; xmm0 < -2^63 -> overflow

    cvttsd2si rax, xmm0
    mov rbx, rax
    jmp .have_int

.overflow:
    mov word [rdi], '?'
    dec rdi
    jmp .finish

.have_int:
    push rax
    cvtsi2sd xmm1, rax
    subsd xmm0, xmm1
    movsd qword [rsp + 8], xmm0

    pop rax
    mov rcx, 10
    test rax, rax
    jnz .int_loop
    mov byte [rdi], '0'
    dec rdi
    jmp .dot

.int_loop:
    xor rdx, rdx
    div rcx
    add dl, '0'
    mov [rdi], dl
    dec rdi
    test rax, rax
    jnz .int_loop

.dot:
    inc rdi
    push rdi

    dec rdi
    mov byte [rdi], '.'
    dec rdi

    movsd xmm0, qword [rsp + 8]
    mov rsi, 6

.frac_loop:
    test rsi, rsi
    jz .frac_done

    movsd xmm1, [.ten]
    mulsd xmm0, xmm1
    cvttsd2si rax, xmm0

    cmp rax, 0
    jl .frac_done
    cmp rax, 9
    jg .frac_done

    add al, '0'
    mov [rdi], al
    dec rdi

    cvtsi2sd xmm1, rax
    subsd xmm0, xmm1

    dec rsi
    jmp .frac_loop

.frac_done:
    inc rdi
    pop rsi

.finish:
    inc rdi
    mov rax, rdi

.ret:
    add rsp, 40
    pop rsi
    pop rdi
    pop rbx
    ret

section '.data' data readable writeable
.buf: rb 64
align 8
.ten: dq 10.0
align 8
.str_nan: db 'nan', 0
align 8
.str_inf: db 'inf', 0
align 8
.str_neg_inf: db '-inf', 0
align 8
