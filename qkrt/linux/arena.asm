; linux/arena.asm
; Linux x86_64, FASM
; Quark Runtime - Arena memory management via mmap/munmap

format ELF64

public qk_std__arena___create
public qk_std__arena___destroy
public qk_std__arena___region_mmap
public qk_std__arena___region_munmap
public qk_std__arena___region_bump
public qk_std__arena___global_region
public qk_std__arena___init_global_region

SYS_MMAP   equ 9
SYS_MUNMAP equ 11

PROT_READ  equ 1
PROT_WRITE equ 2

MAP_PRIVATE    equ 0x02
MAP_ANONYMOUS  equ 0x20

section '.text' executable

; =====================================================
; Legacy functions (Windows ABI for region codegen)
; =====================================================

; void* qk_std__arena___create(size)
; rcx = size in bytes
; Returns: pointer in rax, or -1 on error
qk_std__arena___create:
    mov rdi, 0             ; addr = NULL (let kernel choose)
    mov rsi, rcx           ; length = size
    mov rdx, PROT_READ or PROT_WRITE ; prot
    mov r10, MAP_PRIVATE or MAP_ANONYMOUS ; flags
    mov r8, -1             ; fd = -1 (not used with MAP_ANONYMOUS)
    mov r9, 0              ; offset = 0
    mov rax, SYS_MMAP
    syscall
    ret

; void qk_std__arena___destroy(ptr, size)
; rcx = pointer
; rdx = size
qk_std__arena___destroy:
    mov rdi, rcx           ; addr
    mov rsi, rdx           ; length
    mov rax, SYS_MUNMAP
    syscall
    ret

; =====================================================
; Region API (Linux System V ABI - args in rdi, rsi)
; =====================================================

; =====================================================
; void* qk_std__arena___region_mmap(size)
; rdi = size in bytes
; Returns: pointer in rax
; =====================================================
qk_std__arena___region_mmap:
    mov rsi, rdi           ; length = size
    xor edi, edi           ; addr = NULL
    mov rdx, PROT_READ or PROT_WRITE
    mov r10, MAP_PRIVATE or MAP_ANONYMOUS
    mov r8, -1
    xor r9, r9
    mov rax, SYS_MMAP
    syscall
    ret

; =====================================================
; void qk_std__arena___region_munmap(ptr, size)
; rdi = pointer
; rsi = size
; =====================================================
qk_std__arena___region_munmap:
    mov rax, SYS_MUNMAP
    syscall
    ret

; =====================================================
; void* qk_std__arena___region_bump(region, size)
; rdi = Region* pointer
; rsi = size in bytes
; Returns: allocated pointer in rax, or NULL on OOM
; Region layout: [data:8] [offset:8] [capacity:8]
; =====================================================
qk_std__arena___region_bump:
    ; align size to 16 bytes
    lea rax, [rsi + 15]
    and rax, -16

    ; check bounds: offset + aligned_size <= capacity
    mov rdx, [rdi + 8]        ; rdx = offset
    mov rcx, rdx
    add rcx, rax              ; rcx = offset + aligned_size
    cmp rcx, [rdi + 16]      ; compare with capacity
    ja .oom

    ; result = data + offset
    mov r8, [rdi + 0]         ; r8 = data
    add r8, rdx               ; r8 = data + offset

    ; update offset
    mov [rdi + 8], rcx

    mov rax, r8
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
; rdi = size in bytes
; Initializes the global region with given capacity
; =====================================================
qk_std__arena___init_global_region:
    push rdi                ; save size on stack
    mov rsi, rdi            ; rsi = size (for mmap length arg)
    xor edi, edi            ; addr = NULL
    mov rdx, PROT_READ or PROT_WRITE
    mov r10, MAP_PRIVATE or MAP_ANONYMOUS
    mov r8, -1
    xor r9, r9
    mov rax, SYS_MMAP
    syscall
    ; rax = mmap'd data pointer

    pop rcx                 ; rcx = size (capacity)

    lea rdx, [qk_std__arena__global_data]
    mov [rdx + 0], rax      ; data
    mov qword [rdx + 8], 0  ; offset = 0
    mov [rdx + 16], rcx     ; capacity = size
    ret

section '.data' writeable
qk_std__arena__global_data:
    dq 0    ; data
    dq 0    ; offset
    dq 0    ; capacity
