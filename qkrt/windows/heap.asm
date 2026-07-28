; windows/heap.asm
; Windows x64, FASM
; Quark Runtime - Heap memory allocator (malloc/free/realloc)
;
; Uses a first-fit free-list on top of VirtualAlloc'd pages.
; Each block has a 16-byte header:
;   [+0] size_flags (qword) - total block size, bit 0 = 1 if allocated
;   [+8] next (qword)       - next free block (only valid when free)
; User data starts at +16 and is 16-byte aligned.
; Minimum block size = 32 bytes.

CHUNK_SIZE    equ 65536       ; 64 KB per chunk
HEADER_SIZE   equ 16          ; size of block header
MIN_BLOCK     equ 32          ; header + minimum payload (16 bytes)
ALIGN_MASK    equ 15          ; 16-byte alignment

section '.bss' writeable
    heap_head: dq 0           ; pointer to first free block

section '.text' code readable executable

; ============================================================
; void* qk_std__heap__heap_malloc(size)
; rcx = size in bytes
; Returns: 16-byte aligned pointer, or NULL on OOM
; ============================================================
qk_std__heap__heap_malloc:
    ; Calculate aligned total = align16(size) + HEADER_SIZE, clamp to MIN_BLOCK
    lea rax, [rcx + HEADER_SIZE + ALIGN_MASK]
    and rax, -16
    cmp rax, MIN_BLOCK
    jae .w_size_ok
    mov rax, MIN_BLOCK
.w_size_ok:
    mov r12, rax                    ; r12 = needed block size

    push rbx
    push r12
    push r13
    sub rsp, 32                     ; shadow space (for VirtualAlloc calls)

.malloc_retry:
    mov rbx, [heap_head]            ; rbx = current block in free list
    xor r13, r13                    ; r13 = previous block (NULL)

.search:
    test rbx, rbx
    jz .refill

    mov rax, [rbx]
    and rax, -2

    cmp rax, r12
    jae .found

    mov r13, rbx
    mov rbx, [rbx + 8]
    jmp .search

.found:
    sub rax, r12
    cmp rax, MIN_BLOCK
    jb .exact_fit

    ; Split from the end
    mov [rbx], rax

    lea rcx, [rbx + rax]
    mov rdx, r12
    or rdx, 1
    mov [rcx], rdx
    lea rax, [rcx + HEADER_SIZE]
    jmp .done

.exact_fit:
    mov rax, [rbx]
    or rax, 1
    mov [rbx], rax

    test r13, r13
    jnz .unlink_from_prev

    mov rax, [rbx + 8]
    mov [heap_head], rax
    jmp .return_ptr

.unlink_from_prev:
    mov rax, [rbx + 8]
    mov [r13 + 8], rax

.return_ptr:
    lea rax, [rbx + HEADER_SIZE]
    jmp .done

.refill:
    ; VirtualAlloc(NULL, CHUNK_SIZE, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE)
    xor ecx, ecx                    ; lpAddress = NULL
    mov rdx, CHUNK_SIZE             ; dwSize
    mov r8d, 0x3000                 ; MEM_COMMIT | MEM_RESERVE
    mov r9d, 4                      ; PAGE_READWRITE
    call [VirtualAlloc]
    test rax, rax
    jz .oom

    ; Set up block header
    mov rdx, CHUNK_SIZE
    mov [rax], rdx

    ; Insert at head of free list
    mov rcx, [heap_head]
    mov [rax + 8], rcx
    mov [heap_head], rax

    jmp .malloc_retry

.oom:
    xor eax, eax

.done:
    add rsp, 32
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; void qk_std__heap__heap_free(ptr)
; rcx = pointer (or NULL)
; ============================================================
qk_std__heap__heap_free:
    test rcx, rcx
    jz .free_return

    push rbx
    push r12
    sub rsp, 32

    lea rbx, [rcx - HEADER_SIZE]

    mov rax, [rbx]
    and rax, -2
    mov [rbx], rax

    ; Coalesce with next physical block
    mov r12, [rbx]
    lea rcx, [rbx + r12]

    mov rax, [rcx]
    test al, 1
    jnz .no_coalesce_next

    mov rdx, [heap_head]
    xor r8, r8

.find_next:
    cmp rdx, rcx
    je .found_next
    test rdx, rdx
    jz .no_coalesce_next
    mov r8, rdx
    mov rdx, [rdx + 8]
    jmp .find_next

.found_next:
    mov rax, [rcx + 8]
    test r8, r8
    jnz .next_not_head
    mov [heap_head], rax
    jmp .merge_next
.next_not_head:
    mov [r8 + 8], rax

.merge_next:
    mov rax, [rcx]
    and rax, -2
    add [rbx], rax

.no_coalesce_next:
    mov rax, [heap_head]
    mov [rbx + 8], rax
    mov [heap_head], rbx

    add rsp, 32
    pop r12
    pop rbx
.free_return:
    ret

; ============================================================
; void* qk_std__heap__heap_realloc(ptr, new_size)
; rcx = old pointer (or NULL)
; rdx = new size in bytes
; Returns: pointer, or NULL on OOM
; ============================================================
qk_std__heap__heap_realloc:
    test rcx, rcx
    jz qk_std__heap__heap_malloc

    test rdx, rdx
    jnz .realloc_start
    push rcx
    sub rsp, 32
    call qk_std__heap__heap_free
    add rsp, 32
    pop rcx
    xor eax, eax
    ret

.realloc_start:
    push rbx
    push r12
    push r13
    push r14
    sub rsp, 32

    mov r12, rcx                    ; r12 = old pointer
    mov r14, rdx                    ; r14 = new_size (save early)
    lea rbx, [rcx - HEADER_SIZE]    ; rbx = old block header
    mov rax, [rbx]
    and rax, -2                     ; rax = old block size

    lea rcx, [rdx + HEADER_SIZE + ALIGN_MASK]
    and rcx, -16
    cmp rcx, MIN_BLOCK
    jae .w_rsize_ok
    mov rcx, MIN_BLOCK
.w_rsize_ok:
    mov r13, rcx                    ; r13 = new block size needed

    cmp r13, rax
    ja .try_extend

    sub rax, r13
    cmp rax, MIN_BLOCK
    jb .realloc_done

    lea r14, [rbx + r13]
    mov [rbx], r13
    or byte [rbx], 1

    mov [r14], rax

    push rdi
    lea rcx, [r14 + HEADER_SIZE]
    call qk_std__heap__heap_free
    pop rdi

    mov rax, r12
    jmp .realloc_done

.try_extend:
    mov rax, [rbx]
    and rax, -2                     ; rax = old block size
    mov r8, rax                     ; r8 = old block size
    lea rcx, [rbx + rax]           ; rcx = next physical block
    mov rax, [rcx]
    test al, 1
    jnz .alloc_new                  ; next is allocated

    mov r9, [rcx]
    and r9, -2                      ; r9 = next block size
    mov rax, r8
    add rax, r9                     ; rax = combined size
    cmp rax, r13
    jb .alloc_new                   ; still not big enough

    ; Remove next block from free list
    mov rdx, [heap_head]
    xor r10, r10
.find_next_ext:
    cmp rdx, rcx
    je .found_next_ext
    test rdx, rdx
    jz .alloc_new
    mov r10, rdx
    mov rdx, [rdx + 8]
    jmp .find_next_ext

.found_next_ext:
    mov rax, [rcx + 8]
    test r10, r10
    jnz .next_ext_not_head
    mov [heap_head], rax
    jmp .merge_ext
.next_ext_not_head:
    mov [r10 + 8], rax

.merge_ext:
    mov rax, [rcx]
    and rax, -2
    add rax, r8                     ; r8 = old block size
    mov [rbx], rax
    or byte [rbx], 1

    mov rax, [rbx]
    and rax, -2
    sub rax, r13
    cmp rax, MIN_BLOCK
    jb .realloc_done

    lea rcx, [rbx + r13]
    mov [rbx], r13
    or byte [rbx], 1
    mov [rcx], rax

    push rdi
    lea rcx, [rcx + HEADER_SIZE]
    call qk_std__heap__heap_free
    pop rdi

    mov rax, r12
    jmp .realloc_done

.alloc_new:
    mov rcx, r14                    ; rcx = new_size (original request)
    call qk_std__heap__heap_malloc
    test rax, rax
    jz .realloc_oom

    ; rdx = min(old_usable, new_usable)
    mov rcx, [rbx]
    and rcx, -2
    sub rcx, HEADER_SIZE            ; rcx = old usable bytes
    mov rdx, r13
    sub rdx, HEADER_SIZE            ; rdx = new usable bytes
    cmp rcx, rdx
    cmovb rdx, rcx                  ; rdx = min(old_usable, new_usable)

    mov rdi, rax                    ; rdi = dest (new ptr)
    mov rsi, r12                    ; rsi = src (old ptr)
    mov rcx, rdx
    rep movsb

    push rax                        ; save new ptr
    mov rcx, r12                    ; free old pointer
    call qk_std__heap__heap_free
    pop rax                         ; return new ptr
    jmp .realloc_done

.realloc_oom:
    xor eax, eax

.realloc_done:
    add rsp, 32
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
