; common/arena.asm
; Quark Runtime - Arena memory management
; Uses VirtualAlloc (Windows) / mmap (Linux) for backing memory
;
; On Windows, included into PE assembly via fasm's include directive.
; VirtualAlloc/VirtualFree are resolved via the PE import table.

section '.text' code readable executable

; ================================================================
; void* qk_std__arena___create(size)
; Allocates a page-aligned memory block for arena usage.
; rcx = size in bytes (will be rounded up to page boundary by VirtualAlloc)
; Returns: pointer to memory block in rax (NULL on failure)
; ================================================================
qk_std__arena___create:
    sub rsp, 40
    mov rdx, rcx        ; dwSize = size
    xor ecx, ecx        ; lpAddress = NULL (system chooses address)
    mov r8d, 0x3000     ; flAllocationType = MEM_COMMIT | MEM_RESERVE
    mov r9d, 4          ; flProtect = PAGE_READWRITE
    call [VirtualAlloc]
    add rsp, 40
    ret

; ================================================================
; void qk_std__arena___destroy(ptr, size)
; Frees a memory block previously allocated with qk_std__arena___create.
; rcx = pointer to memory block
; rdx = size (unused for MEM_RELEASE, but kept for ABI consistency)
; ================================================================
qk_std__arena___destroy:
    sub rsp, 40
    xor edx, edx        ; dwSize = 0 (required for MEM_RELEASE)
    mov r8d, 0x8000     ; dwFreeType = MEM_RELEASE
    call [VirtualFree]
    add rsp, 40
    ret
