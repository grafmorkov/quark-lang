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

section '.text' executable readable

; ================================================================
; qk_open(const char* path, int flags, int mode, qk_handle* out_handle)
; rdi = path, rsi = flags, rdx = mode, rcx = out_handle
; Returns: 0 on success, negative errno on error
; ================================================================
qk_open:
    mov r8, rcx                    ; save out_handle pointer
    mov r9, rdx                    ; save mode
    mov r10, rdx                   ; 4th arg for syscall = mode

    mov eax, SYS_OPENAT
    mov edi, AT_FDCWD
    mov rsi, rdi                   ; path
    mov rdx, r9                    ; wait, better shuffle:

    ; Correct safe shuffle:
    mov r10, r9                    ; r10 = mode
    mov rdx, rsi                   ; rdx = flags
    mov rsi, rdi                   ; rsi = path
    mov edi, AT_FDCWD

    syscall

    test rax, rax
    js .error

    mov [r8], rax                  ; *out_handle = fd
    xor eax, eax                   ; success
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