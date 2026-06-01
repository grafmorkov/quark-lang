; linux/file.asm
; Linux x86_64, FASM
; Quark Runtime File API - follows ABI contract

format ELF64

public qk_open
public qk_close
public qk_seek
public qk_flush
public qk_write_fd
public qk_read_fd

; Linux syscalls
SYS_OPENAT  equ 257
SYS_CLOSE   equ 3
SYS_LSEEK   equ 8
SYS_FSYNC   equ 74
SYS_WRITE   equ 1
SYS_READ    equ 0

AT_FDCWD    equ -100

section '.text' executable

; ================================================================
; i64 qk_open(const char* path, int flags, int mode)
; rdi = path, rsi = flags, rdx = mode
; Returns: fd (positive) on success, negative errno on error
; ================================================================
qk_open:
    mov r10, rdx             ; r10 = mode (4th syscall arg)
    mov rdx, rsi             ; rdx = flags
    mov rsi, rdi             ; rsi = path
    mov eax, SYS_OPENAT
    mov edi, AT_FDCWD
    syscall
    test rax, rax
    js .error
    ret
.error:
    neg rax
    ret


; ================================================================
; isize qk_close(qk_handle h)
; rdi = handle
; ================================================================
qk_close:
    mov eax, SYS_CLOSE
    syscall
    test rax, rax
    jns .ok
    neg rax
.ok:
    ret


; ================================================================
; isize qk_seek(qk_handle h, i64 offset, int whence)
; rdi = handle, rsi = offset, rdx = whence
; ================================================================
qk_seek:
    mov eax, SYS_LSEEK
    syscall
    test rax, rax
    jns .ok
    neg rax
.ok:
    ret


; ================================================================
; isize qk_flush(qk_handle h)
; rdi = handle
; ================================================================
qk_flush:
    mov eax, SYS_FSYNC
    syscall
    test rax, rax
    jns .ok
    neg rax
.ok:
    ret


; ================================================================
; ssize_t qk_write_fd(qk_handle h, const void* buf, size_t len)
; rdi = h, rsi = buf, rdx = len
; ================================================================
qk_write_fd:
    mov eax, SYS_WRITE
    syscall
    test rax, rax
    jns .ok
    neg rax
.ok:
    ret


; ================================================================
; ssize_t qk_read_fd(qk_handle h, void* buf, size_t len)
; rdi = h, rsi = buf, rdx = len
; ================================================================
qk_read_fd:
    mov eax, SYS_READ
    syscall
    test rax, rax
    jns .ok
    neg rax
.ok:
    ret

; ABI aliases: std::io::* -> runtime functions
public qk_std__io__open
public qk_std__io__close
public qk_std__io__seek
public qk_std__io__flush
public qk_std__io__write
public qk_std__io__read_fd

qk_std__io__open:
    jmp qk_open
qk_std__io__close:
    jmp qk_close
qk_std__io__seek:
    jmp qk_seek
qk_std__io__flush:
    jmp qk_flush
qk_std__io__write:
    jmp qk_write_fd
qk_std__io__read_fd:
    jmp qk_read_fd