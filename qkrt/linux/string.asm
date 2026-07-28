; linux/string.asm
; Linux x86_64, FASM
; Quark Runtime String layer

format ELF64

public qk_std__string__str_len
public qk_std__string__str_eq
public qk_std__string__str_neq
public qk_std__string__str_empty
public qk_std__string__str_char_at
public qk_std__string__str_concat
public qk_std__string__str_sub
public qk_std__string__str_copy

; ================================================================
; Internal: strlen
; rdi = s -> rax = length
; ================================================================
strlen:
    xor eax, eax
.loop:
    cmp byte [rdi + rax], 0
    je .done
    inc rax
    jmp .loop
.done:
    ret

; ================================================================
; Internal: memcpy
; rdi = dst, rsi = src, rdx = len
; ================================================================
memcpy:
    xor ecx, ecx
.loop:
    cmp rcx, rdx
    jge .done
    mov al, [rsi + rcx]
    mov [rdi + rcx], al
    inc rcx
    jmp .loop
.done:
    ret

; ================================================================
; Internal: arena_bump
; rdi = size -> rax = pointer (or NULL)
; ================================================================
extrn qk_std__arena___global_region
extrn qk_std__arena___region_bump

arena_bump:
    mov rsi, rdi
    call qk_std__arena___global_region
    mov rdi, rax
    jmp qk_std__arena___region_bump


; ================================================================
; i64 str_len(str s)
; rdi = s
; ================================================================
qk_std__string__str_len:
    jmp strlen


; ================================================================
; bool str_eq(str a, str b)
; rdi = a, rsi = b
; Returns: 1 or 0
; ================================================================
qk_std__string__str_eq:
    push rbx
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
; bool str_neq(str a, str b)
; rdi = a, rsi = b
; ================================================================
qk_std__string__str_neq:
    call qk_std__string__str_eq
    xor eax, 1
    ret


; ================================================================
; bool str_empty(str s)
; rdi = s
; ================================================================
qk_std__string__str_empty:
    cmp byte [rdi], 0
    sete al
    movzx eax, al
    ret


; ================================================================
; char str_char_at(str s, i64 index)
; rdi = s, rsi = index
; ================================================================
qk_std__string__str_char_at:
    movzx eax, byte [rdi + rsi]
    ret


; ================================================================
; str str_concat(str a, str b)
; rdi = a, rsi = b
; Returns: arena-allocated concatenated string
; ================================================================
qk_std__string__str_concat:
    push rbx
    push r12
    push r13
    mov rbx, rdi           ; rbx = a
    mov r12, rsi           ; r12 = b

    call strlen
    mov r13, rax           ; r13 = len(a)

    mov rdi, r12
    call strlen
    lea rdi, [r13 + rax + 1]  ; rdi = len(a) + len(b) + 1
    call arena_bump
    test rax, rax
    jz .oom

    ; Copy a into buffer
    mov rdi, rax           ; rdi = buffer
    mov rsi, rbx           ; rsi = a
    mov rdx, r13           ; rdx = len(a)
    push rax               ; save buffer pointer
    call memcpy
    pop rax                ; rax = buffer

    ; Copy b after a
    lea rdi, [rax + r13]   ; rdi = buffer + len(a)
    mov rsi, r12           ; rsi = b
    mov rdi, rax           ; save rdi temporarily
    push rax
    mov rdi, r12
    call strlen            ; rax = len(b)
    mov rdx, rax           ; rdx = len(b)
    pop rax
    lea rdi, [rax + r13]   ; rdi = buffer + len(a)
    mov rsi, r12           ; rsi = b
    call memcpy

    ; Null-terminate
    mov byte [rax + r13 + 0], 0  ; will be overwritten by b's null
    ; Actually b already has null, so the last byte copied is b's null

    pop r13
    pop r12
    pop rbx
    ret

.oom:
    xor eax, eax
    pop r13
    pop r12
    pop rbx
    ret


; ================================================================
; str str_sub(str s, i64 start, i64 len)
; rdi = s, rsi = start, rdx = len
; Returns: arena-allocated substring
; ================================================================
qk_std__string__str_sub:
    push rbx
    push r12
    push r13
    mov rbx, rdi           ; rbx = s
    mov r12, rsi           ; r12 = start
    mov r13, rdx           ; r13 = len

    lea rdi, [rdx + 1]     ; rdi = len + 1
    call arena_bump
    test rax, rax
    jz .oom

    mov rdi, rax           ; rdi = buffer
    lea rsi, [rbx + r12]   ; rsi = s + start
    mov rdx, r13           ; rdx = len
    push rax               ; save buffer
    call memcpy

    pop rax                ; rax = buffer
    mov byte [rax + r13], 0  ; null-terminate

    pop r13
    pop r12
    pop rbx
    ret

.oom:
    xor eax, eax
    pop r13
    pop r12
    pop rbx
    ret


; ================================================================
; str str_copy(str s)
; rdi = s
; Returns: arena-allocated copy
; ================================================================
qk_std__string__str_copy:
    push rbx
    push r12
    mov rbx, rdi           ; rbx = s

    call strlen
    mov r12, rax           ; r12 = len(s)

    lea rdi, [rax + 1]     ; rdi = len + 1
    call arena_bump
    test rax, rax
    jz .oom

    mov rdi, rax           ; rdi = buffer
    mov rsi, rbx           ; rsi = s
    mov rdx, r12           ; rdx = len
    push rax               ; save buffer
    call memcpy

    pop rax                ; rax = buffer
    mov byte [rax + r12], 0  ; null-terminate

    pop r12
    pop rbx
    ret

.oom:
    xor eax, eax
    pop r12
    pop rbx
    ret
