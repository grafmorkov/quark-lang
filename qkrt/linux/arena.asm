; linux/arena.asm
; Linux x86_64, FASM
; Quark Runtime - Arena memory management via mmap/munmap

format ELF64

public qk_std__arena___create
public qk_std__arena___destroy

SYS_MMAP   equ 9
SYS_MUNMAP equ 11

PROT_READ  equ 1
PROT_WRITE equ 2

MAP_PRIVATE    equ 0x02
MAP_ANONYMOUS  equ 0x20

section '.text' executable
; =====================================================
; void* qk_std__arena___create(size)
; Allocates a page-aligned memory block via mmap.
; rdi = size in bytes
; Returns: pointer in rax, or -1 on error
; =====================================================vc
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
; =====================================================
; void qk_std__arena___destroy(ptr, size)
; Frees a memory block via munmap.
; rcx = pointer
; rdx = size
; =====================================================
qk_std__arena___destroy:
    mov rdi, rcx           ; addr
    mov rsi, rdx           ; length
    mov rax, SYS_MUNMAP
    syscall
    ret
