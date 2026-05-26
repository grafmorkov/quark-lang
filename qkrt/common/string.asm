; common/string.asm

section '.text' code readable executable

; size_t qk_strlen_impl(const char* s)
; in:
;   rdi = line
; out:
;   rax = length to zero
qk_strlen_impl:
    xor eax, eax

.loop:
    cmp byte [rdi + rax], 0
    je .done
    inc rax
    jmp .loop

.done:
    ret