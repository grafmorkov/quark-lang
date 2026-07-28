; windows/string.asm
; Windows x86_64, FASM
; Quark Runtime String layer

format PE64

public qk_std__string__str_len
public qk_std__string__str_eq
public qk_std__string__str_neq
public qk_std__string__str_empty
public qk_std__string__str_char_at
public qk_std__string__str_concat
public qk_std__string__str_sub
public qk_std__string__str_copy

; ================================================================
; Internal: strlen (rcx = s → rax = length)
; ================================================================
strlen:
    xor eax, eax
.loop:
    cmp byte [rcx + rax], 0
    je .done
    inc rax
    jmp .loop
.done:
    ret

; ================================================================
; Internal: memcpy (rcx = dst, rdx = src, r8 = len)
; ================================================================
memcpy:
    xor eax, eax
.loop:
    cmp rax, r8
    jge .done
    mov r9b, [rdx + rax]
    mov [rcx + rax], r9b
    inc rax
    jmp .loop
.done:
    ret

; ================================================================
; Internal: arena_bump (rcx = size → rax = pointer)
; ================================================================
extern qk_std__arena___global_region
extern qk_std__arena___region_bump

arena_bump:
    mov rdx, rcx
    sub rsp, 32
    call qk_std__arena___global_region
    add rsp, 32
    mov rcx, rax
    jmp qk_std__arena___region_bump


; ================================================================
; i64 str_len(str s) — rcx = s
; ================================================================
qk_std__string__str_len:
    jmp strlen


; ================================================================
; bool str_eq(str a, str b) — rcx = a, rdx = b
; ================================================================
qk_std__string__str_eq:
    push rbx
    mov rdi, rcx
    mov rsi, rdx
.loop:
    movzx eax, byte [rdi]
    movzx ebx, byte [rsi]
    cmp al, bl
    jne .no
    test al, al
    jz .yes
    inc rdi
    inc rsi
    jmp .loop
.yes:
    mov eax, 1
    pop rbx
    ret
.no:
    xor eax, eax
    pop rbx
    ret


; ================================================================
; bool str_neq(str a, str b) — rcx = a, rdx = b
; ================================================================
qk_std__string__str_neq:
    sub rsp, 32
    call qk_std__string__str_eq
    add rsp, 32
    xor eax, 1
    ret


; ================================================================
; bool str_empty(str s) — rcx = s
; ================================================================
qk_std__string__str_empty:
    cmp byte [rcx], 0
    sete al
    movzx eax, al
    ret


; ================================================================
; char str_char_at(str s, i64 index) — rcx = s, rdx = index
; ================================================================
qk_std__string__str_char_at:
    movzx eax, byte [rcx + rdx]
    ret


; ================================================================
; str str_concat(str a, str b) — rcx = a, rdx = b
; ================================================================
qk_std__string__str_concat:
    push rbx
    push r12
    push r13
    sub rsp, 32
    mov rbx, rcx
    mov r12, rdx

    mov rcx, rbx
    call strlen
    mov r13, rax

    mov rcx, r12
    call strlen
    lea rcx, [r13 + rax + 1]
    call arena_bump
    test rax, rax
    jz .oom

    ; Copy a
    mov rdi, rax
    mov rsi, rbx
    mov rdx, r13
    push rax
    call memcpy
    pop rax

    ; Copy b after a
    lea rcx, [rax + r13]
    mov rdx, r12
    mov r8, r12
    push rax
    mov rcx, r12
    call strlen
    mov r8, rax
    pop rax
    lea rcx, [rax + r13]
    mov rdx, r12
    call memcpy

    add rsp, 32
    pop r13
    pop r12
    pop rbx
    ret

.oom:
    xor eax, eax
    add rsp, 32
    pop r13
    pop r12
    pop rbx
    ret


; ================================================================
; str str_sub(str s, i64 start, i64 len)
; rcx = s, rdx = start, r8 = len
; ================================================================
qk_std__string__str_sub:
    push rbx
    push r12
    push r13
    sub rsp, 32
    mov rbx, rcx
    mov r12, rdx
    mov r13, r8

    lea rcx, [r8 + 1]
    call arena_bump
    test rax, rax
    jz .oom

    mov rdi, rax
    lea rsi, [rbx + r12]
    mov rdx, r13
    push rax
    call memcpy
    pop rax

    mov byte [rax + r13], 0

    add rsp, 32
    pop r13
    pop r12
    pop rbx
    ret

.oom:
    xor eax, eax
    add rsp, 32
    pop r13
    pop r12
    pop rbx
    ret


; ================================================================
; str str_copy(str s) — rcx = s
; ================================================================
qk_std__string__str_copy:
    push rbx
    push r12
    sub rsp, 32
    mov rbx, rcx

    call strlen
    mov r12, rax

    lea rcx, [rax + 1]
    call arena_bump
    test rax, rax
    jz .oom

    mov rdi, rax
    mov rsi, rbx
    mov rdx, r12
    push rax
    call memcpy
    pop rax

    mov byte [rax + r12], 0

    add rsp, 32
    pop r12
    pop rbx
    ret

.oom:
    xor eax, eax
    add rsp, 32
    pop r12
    pop rbx
    ret
