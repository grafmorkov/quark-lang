; windows/io.asm
; Windows x64, FASM
; Quark Runtime IO layer (WinAPI-based)

QK_STD_INPUT_HANDLE  equ -10
QK_STD_OUTPUT_HANDLE equ -11
QK_STD_ERROR_HANDLE  equ -12

section '.data' data readable writeable
qk_stdout_handle dq 0
qk_stdin_handle  dq 0
qk_stderr_handle dq 0
section '.text' code readable executable

; void qk_io_init(void)
; Caches standard handles (stdout, stdin, stderr).
; Must be called once at program startup.
qk_io_init:
    sub rsp, 40

    mov ecx, QK_STD_OUTPUT_HANDLE
    call [GetStdHandle]
    mov [qk_stdout_handle], rax

    mov ecx, QK_STD_INPUT_HANDLE
    call [GetStdHandle]
    mov [qk_stdin_handle], rax

    mov ecx, QK_STD_ERROR_HANDLE
    call [GetStdHandle]
    mov [qk_stderr_handle], rax

    add rsp, 40
    ret


; ssize_t qk_print(const void* buf, size_t len)
; Writes to stdout
; rcx = buf, rdx = len
qk_print:
    mov r8, rdx
    mov rdx, rcx
    mov rcx, [qk_stdout_handle]
    jmp qk_write_fd


; ssize_t qk_read(void* buf, size_t len)
; Reads from stdin
; rcx = buf, rdx = len
qk_read:
    mov r8, rdx
    mov rdx, rcx
    mov rcx, [qk_stdin_handle]
    jmp qk_read_fd


; ssize_t qk_eprint(const void* buf, size_t len)
; Writes to stderr
; rcx = buf, rdx = len
qk_eprint:
    mov r8, rdx
    mov rdx, rcx
    mov rcx, [qk_stderr_handle]
    jmp qk_write_fd


; ssize_t qk_putc(char c)
; Writes single byte to stdout
; cl = byte to write
qk_putc:
    sub rsp, 56
    mov byte [rsp + 32], cl

    lea rcx, [rsp + 32]
    mov rdx, 1
    call qk_print

    add rsp, 56
    ret


; ssize_t qk_eputc(char c)
; Writes single byte to stderr
; cl = byte to write
qk_eputc:
    sub rsp, 56
    mov byte [rsp + 32], cl

    lea rcx, [rsp + 32]
    mov rdx, 1
    call qk_eprint

    add rsp, 56
    ret


; void qk_exit(int code)
; Terminates the process
; ecx = exit code
qk_exit:
    sub rsp, 40
    call [ExitProcess]
    add rsp, 40
    ret


; size_t qk_strlen(const char* s)
; rcx = pointer to null-terminated string
; Returns: string length (excluding null terminator)
qk_strlen:
    mov rdi, rcx
    jmp qk_strlen_impl


; ssize_t qk_printz(const char* s)
; Writes null-terminated string to stdout
; rcx = pointer to string
qk_printz:
    push rcx
    call qk_strlen
    pop rcx

    mov rdx, rax
    call qk_print
    ret


; ssize_t qk_eprintz(const char* s)
; Writes null-terminated string to stderr
; rcx = pointer to string
qk_eprintz:
    push rcx
    call qk_strlen
    pop rcx

    mov rdx, rax
    call qk_eprint
    ret

; ABI aliases: std::io::* -> runtime functions
qk_std__io__print:
    jmp qk_printz
qk_std__io__println:
    call qk_printz
    mov rcx, 10
    jmp qk_putc
qk_std__io__eprint:
    jmp qk_eprintz
qk_std__io__print_char:
    jmp qk_putc
qk_std__io__exit:
    jmp qk_exit
qk_std__io__strlen:
    jmp qk_strlen