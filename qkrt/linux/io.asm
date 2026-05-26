; linux/io.asm
; Linux x86_64, FASM
; Quark Runtime IO layer (syscall-based)

format ELF64

; ================================================================
; Public symbols
; ================================================================

public qk_io_init
public qk_print
public qk_eprint
public qk_read
public qk_putc
public qk_getc
public qk_eputc
public qk_exit
public qk_strlen
public qk_printz
public qk_eprintz


extrn qk_strlen_impl

; Syscall numbers
SYS_WRITE   equ 1
SYS_READ    equ 0
SYS_EXIT    equ 60

section '.text' executable readable

; ================================================================
; Initialization
; ================================================================

; void qk_io_init(void)
; On Linux this is a no-op (we use fixed fds 0,1,2)
; Kept for ABI compatibility with Windows version.
qk_io_init:
    ret


; ================================================================
; Stream helpers
; ================================================================

; ssize_t qk_print(const void* buf, size_t len)
; Writes to stdout (fd = 1)
qk_print:
    mov rdi, 1
    jmp qk_write_fd


; ssize_t qk_eprint(const void* buf, size_t len)
; Writes to stderr (fd = 2)
qk_eprint:
    mov rdi, 2
    jmp qk_write_fd


; ssize_t qk_read(void* buf, size_t len)
; Reads from stdin (fd = 0)
qk_read:
    mov rdx, rsi     ; rdx = len
    mov rsi, rdi     ; rsi = buf
    xor edi, edi     ; rdi = 0 (stdin fd)
    jmp qk_read_fd


; ssize_t qk_getc()
; Reads one byte from stdin
; Returns: byte value (0-255), or -1 on error/EOF
qk_getc:
    sub rsp, 16
    lea rdi, [rsp]
    mov esi, 1
    call qk_read
    test rax, rax
    jle .error
    movzx eax, byte [rsp]
    add rsp, 16
    ret
.error:
    or rax, -1
    add rsp, 16
    ret


; ssize_t qk_putc(char c)
; Writes one byte to stdout
qk_putc:
    sub rsp, 8
    mov [rsp], dil
    mov rsi, rsp
    mov rdx, 1
    mov rdi, 1
    call qk_write_fd
    add rsp, 8
    ret


; ssize_t qk_eputc(char c)
; Writes one byte to stderr
qk_eputc:
    sub rsp, 8
    mov [rsp], dil
    mov rsi, rsp
    mov rdx, 1
    mov rdi, 2
    call qk_write_fd
    add rsp, 8
    ret


; ================================================================
; String utilities
; ================================================================

; size_t qk_strlen(const char* s)
qk_strlen:
    mov rdi, rdi
    jmp qk_strlen_impl


; ssize_t qk_printz(const char* s)
qk_printz:
    push rdi
    call qk_strlen
    pop rdi
    mov rdx, rax
    jmp qk_print


; ssize_t qk_eprintz(const char* s)
qk_eprintz:
    push rdi
    call qk_strlen
    pop rdi
    mov rdx, rax
    jmp qk_eprint


; ================================================================
; Process control
; ================================================================

; void qk_exit(int code) — noreturn
qk_exit:
    mov eax, SYS_EXIT
    syscall
    ; does not return
    ret

; ABI aliases: std::io::* -> runtime functions
public qk_std__io__print
public qk_std__io__println
public qk_std__io__eprint
public qk_std__io__print_char
public qk_std__io__exit
public qk_std__io__strlen
public qk_std__io__read
public qk_std__io__read_char

qk_std__io__print:
    jmp qk_printz
qk_std__io__println:
    call qk_printz
    push 10
    mov rdi, rsp
    call qk_putc
    pop rax
    ret
qk_std__io__eprint:
    jmp qk_eprintz
qk_std__io__print_char:
    jmp qk_putc
qk_std__io__exit:
    jmp qk_exit
qk_std__io__strlen:
    jmp qk_strlen
qk_std__io__read:
    jmp qk_read
qk_std__io__read_char:
    jmp qk_getc