; windows/file.asm
; Windows x64, FASM
; Quark Runtime File API
; Types: str → const char*, i32 → 4-byte, i64 → 8-byte

section '.data' data readable writeable
    temp_offset dq 0

section '.text' code readable executable

; ================================================================
; i64 qk_open(str path, i32 flags, i32 mode)
; rcx = path, rdx = flags, r8 = mode
; Returns handle in RAX (positive), or -GetLastError() (negative)
; ================================================================
qk_open:
    sub rsp, 56
    ; CreateFileA(lpFileName, dwDesiredAccess, dwShareMode,
    ;             lpSecurityAttributes, dwCreationDisposition,
    ;             dwFlagsAndAttributes, hTemplateFile)
    ; rcx = path (lpFileName)      — already correct
    ; rdx = flags (dwDesiredAccess) — already correct
    mov [rsp + 32], r8       ; dwCreationDisposition = mode
    mov dword [rsp + 40], 0  ; dwFlagsAndAttributes = 0
    mov dword [rsp + 48], 0  ; hTemplateFile = NULL
    xor r9d, r9d             ; lpSecurityAttributes = NULL
    xor r8d, r8d             ; dwShareMode = 0 (no sharing)
    call [CreateFileA]

    cmp rax, -1              ; INVALID_HANDLE_VALUE
    je .fail
    add rsp, 56
    ret
.fail:
    call [GetLastError]
    neg eax
    cdqe
    add rsp, 56
    ret


; ================================================================
; i64 qk_close(i64 h)
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
; i64 qk_seek(i64 h, i64 offset, i32 whence)
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
; i64 qk_flush(i64 h)
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
; i64 qk_write_fd(i64 h, str buf, i64 len)
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
; i64 qk_read_fd(i64 h, *void buf, i64 len)
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