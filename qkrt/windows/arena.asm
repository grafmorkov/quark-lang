; windows/arena.asm
; Windows x64, FASM
; Quark Runtime - Arena memory management
; Uses VirtualAlloc/VirtualFree for backing memory

section '.text' code readable executable

; =====================================================
; Legacy functions (Windows ABI)
; =====================================================

; void* qk_std__arena___create(size)
; rcx = size in bytes
; Returns: pointer in rax (NULL on failure)
qk_std__arena___create:
    sub rsp, 40
    mov rdx, rcx        ; dwSize = size
    xor ecx, ecx        ; lpAddress = NULL
    mov r8d, 0x3000     ; flAllocationType = MEM_COMMIT | MEM_RESERVE
    mov r9d, 4          ; flProtect = PAGE_READWRITE
    call [VirtualAlloc]
    add rsp, 40
    ret

; void qk_std__arena___destroy(ptr, size)
; rcx = pointer
; rdx = size (unused for MEM_RELEASE)
qk_std__arena___destroy:
    sub rsp, 40
    xor edx, edx        ; dwSize = 0 (required for MEM_RELEASE)
    mov r8d, 0x8000     ; dwFreeType = MEM_RELEASE
    call [VirtualFree]
    add rsp, 40
    ret

; =====================================================
; Region API (Windows x64 ABI - args in rcx, rdx)
; =====================================================

; =====================================================
; void* qk_std__arena___region_mmap(size)
; rcx = size in bytes
; Returns: pointer in rax
; =====================================================
qk_std__arena___region_mmap:
    sub rsp, 40
    mov rdx, rcx        ; dwSize = size
    xor ecx, ecx        ; lpAddress = NULL
    mov r8d, 0x3000     ; MEM_COMMIT | MEM_RESERVE
    mov r9d, 4          ; PAGE_READWRITE
    call [VirtualAlloc]
    add rsp, 40
    ret

; =====================================================
; void qk_std__arena___region_munmap(ptr, size)
; rcx = pointer
; rdx = size (unused for MEM_RELEASE)
; =====================================================
qk_std__arena___region_munmap:
    sub rsp, 40
    xor edx, edx        ; dwSize = 0
    mov r8d, 0x8000     ; MEM_RELEASE
    call [VirtualFree]
    add rsp, 40
    ret

; =====================================================
; void* qk_std__arena___region_bump(region, size)
; rcx = Region* pointer
; rdx = size in bytes
; Returns: allocated pointer in rax, or NULL on OOM
; Region layout: [data:8] [offset:8] [capacity:8]
; =====================================================
qk_std__arena___region_bump:
    ; align size to 16 bytes
    lea rax, [rdx + 15]
    and rax, -16

    ; check bounds: offset + aligned_size <= capacity
    mov r8, [rcx + 8]        ; r8 = offset
    mov r9, r8
    add r9, rax              ; r9 = offset + aligned_size
    cmp r9, [rcx + 16]      ; compare with capacity
    ja .oom

    ; result = data + offset
    mov r10, [rcx + 0]       ; r10 = data
    add r10, r8              ; r10 = data + offset

    ; update offset
    mov [rcx + 8], r9

    mov rax, r10
    ret

.oom:
    xor eax, eax              ; return NULL
    ret

; =====================================================
; Region* qk_std__arena___global_region()
; Returns: pointer to global Region in rax
; =====================================================
qk_std__arena___global_region:
    lea rax, [qk_std__arena__global_data]
    ret

; =====================================================
; void qk_std__arena___init_global_region(size)
; rcx = size in bytes
; Initializes the global region with given capacity
; =====================================================
qk_std__arena___init_global_region:
    ; rcx = size
    push rcx                ; save size on stack
    sub rsp, 40             ; shadow space

    mov rdx, rcx            ; dwSize = size
    xor ecx, ecx            ; lpAddress = NULL
    mov r8d, 0x3000         ; MEM_COMMIT | MEM_RESERVE
    mov r9d, 4              ; PAGE_READWRITE
    call [VirtualAlloc]

    add rsp, 40             ; remove shadow space
    pop rcx                 ; rcx = size

    lea rdx, [qk_std__arena__global_data]
    mov [rdx + 0], rax      ; data
    mov qword [rdx + 8], 0  ; offset = 0
    mov [rdx + 16], rcx     ; capacity = size
    ret

section '.data' data readable writeable
qk_std__arena__global_data:
    dq 0    ; data
    dq 0    ; offset
    dq 0    ; capacity
