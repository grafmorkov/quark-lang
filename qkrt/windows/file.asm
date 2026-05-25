; windows/file.asm
; Windows x64, FASM
; Quark Runtime File API - matches Linux ABI

section '.data' data readable writeable
    ; Temporary for seek
    temp_offset dq 0

section '.text' code readable executable

; ================================================================
; isize qk_open(const char* path, int flags, int mode, qk_handle* out_handle)
; rcx = path, rdx = flags, r8 = mode, r9 = out_handle
; ================================================================
qk_open:
    sub rsp, 56

    mov r10, r9                        ; save out_handle

    mov r9, 0                          ; hTemplateFile = NULL
    mov [rsp + 32], r8                 ; dwCreationDisposition = mode (упрощённо)
    mov dword [rsp + 40], 0            ; dwFlagsAndAttributes

    mov r8, rdx                        ; dwDesiredAccess = flags
    mov rdx, rcx                       ; lpFileName

    call [CreateFileA]

    cmp rax, -1                        ; INVALID_HANDLE_VALUE
    je .fail

    mov [r10], rax
    xor eax, eax
    jmp .exit

.fail:
    call [GetLastError]
    neg eax
    cdqe

.exit:
    add rsp, 56
    ret


; ================================================================
; isize qk_close(qk_handle h)
; rcx = handle
; ================================================================
qk_close:
    sub rsp, 40
    call [CloseHandle]
    test eax, eax
    jz .fail
    xor eax, eax
    jmp .exit
.fail:
    call [GetLastError]
    neg eax
    cdqe
.exit:
    add rsp, 40
    ret


; ================================================================
; isize qk_seek(qk_handle h, i64 offset, int whence)
; rcx = h, rdx = offset, r8 = whence
; ================================================================
qk_seek:
    sub rsp, 40

    mov [temp_offset], rdx             ; low 64-bit
    lea rdx, [temp_offset]

    ; DWORD SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove,
    ;                        PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
    mov r9, r8                         ; dwMoveMethod (whence)
    xor r8, r8                         ; lpNewFilePointer = NULL (не возвращаем позицию)
    call [SetFilePointerEx]

    test eax, eax
    jz .fail
    xor eax, eax                       ; success (можно возвращать новую позицию позже)
    jmp .exit

.fail:
    call [GetLastError]
    neg eax
    cdqe

.exit:
    add rsp, 40
    ret


; ================================================================
; isize qk_flush(qk_handle h)
; rcx = handle
; ================================================================
qk_flush:
    sub rsp, 40
    call [FlushFileBuffers]
    test eax, eax
    jz .fail
    xor eax, eax
    jmp .exit
.fail:
    call [GetLastError]
    neg eax
    cdqe
.exit:
    add rsp, 40
    ret


; ================================================================
; ssize_t qk_write_fd(qk_handle h, const void* buf, size_t len)
; rcx = h, rdx = buf, r8 = len
; ================================================================
qk_write_fd:
    sub rsp, 40
    mov qword [rsp + 32], 0            ; lpOverlapped = NULL
    lea r9, [temp_offset]              ; можно использовать как bytesWritten
    call [WriteFile]

    test eax, eax
    jz .fail

    mov eax, dword [temp_offset]       ; bytes written
    add rsp, 40
    ret

.fail:
    call [GetLastError]
    neg eax
    cdqe
    add rsp, 40
    ret


; ================================================================
; ssize_t qk_read_fd(qk_handle h, void* buf, size_t len)
; rcx = h, rdx = buf, r8 = len
; ================================================================
qk_read_fd:
    sub rsp, 40
    mov qword [rsp + 32], 0
    lea r9, [temp_offset]
    call [ReadFile]

    test eax, eax
    jz .fail

    mov eax, dword [temp_offset]
    add rsp, 40
    ret

.fail:
    call [GetLastError]
    neg eax
    cdqe
    add rsp, 40
    ret

; ABI aliases: std::io::* -> runtime functions
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