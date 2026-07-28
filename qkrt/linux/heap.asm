; linux/heap.asm
; Linux x86_64, FASM
; Quark Runtime - Heap memory allocator (malloc/free/realloc)
;
; Uses a first-fit free-list on top of mmap'd chunks.
; Each block has a 16-byte header:
;   [+0] size_flags (qword) - total block size, bit 0 = 1 if allocated
;   [+8] next (qword)       - next free block (only valid when free)
; User data starts at +16 and is 16-byte aligned.
; Minimum block size = 32 bytes.

format ELF64

public qk_std__heap__heap_malloc
public qk_std__heap__heap_free
public qk_std__heap__heap_realloc

CHUNK_SIZE    equ 65536       ; 64 KB per mmap chunk
HEADER_SIZE   equ 16          ; size of block header
MIN_BLOCK     equ 32          ; header + minimum payload (16 bytes)
ALIGN_MASK    equ 15          ; 16-byte alignment

SYS_MMAP      equ 9
SYS_MUNMAP    equ 11
PROT_RW       equ 3           ; PROT_READ | PROT_WRITE
MAP_PRIV_ANON equ 0x22        ; MAP_PRIVATE | MAP_ANONYMOUS

section '.bss' writeable
    heap_head: dq 0           ; pointer to first free block

section '.text' executable

; ============================================================
; void* qk_std__heap__heap_malloc(size)
; rdi = size in bytes
; Returns: 16-byte aligned pointer, or NULL on OOM
; ============================================================
qk_std__heap__heap_malloc:
    ; Calculate aligned total = align16(size) + HEADER_SIZE, clamp to MIN_BLOCK
    lea rax, [rdi + HEADER_SIZE + ALIGN_MASK]
    and rax, -16
    cmp rax, MIN_BLOCK
    jae .size_ok
    mov rax, MIN_BLOCK
.size_ok:
    mov r12, rax                    ; r12 = needed block size

    push rbx
    push r12
    push r13

.malloc_retry:
    mov rbx, [heap_head]            ; rbx = current block in free list
    xor r13, r13                    ; r13 = previous block (NULL)

.search:
    test rbx, rbx
    jz .refill                      ; no more free blocks, get more memory

    mov rax, [rbx]                  ; rax = size_flags
    and rax, -2                     ; clear flag -> actual block size

    cmp rax, r12
    jae .found                      ; this block is big enough

    mov r13, rbx
    mov rbx, [rbx + 8]              ; next free block
    jmp .search

.found:
    ; rax = actual block size, rbx = block address, r12 = needed
    sub rax, r12
    cmp rax, MIN_BLOCK
    jb .exact_fit                   ; remainder too small, use whole block

    ; Split from the end: keep free block at rbx, take from top
    ; Free block stays in the free list, just with smaller size
    mov [rbx], rax                  ; update free block size (flag already 0)

    ; Used block starts at rbx + rax
    lea rcx, [rbx + rax]
    mov rdx, r12
    or rdx, 1                       ; mark as used
    mov [rcx], rdx
    lea rax, [rcx + HEADER_SIZE]    ; return pointer to user data
    jmp .done

.exact_fit:
    ; Remove this block from the free list
    mov rax, [rbx]                  ; rax = size_flags
    or rax, 1                       ; mark as used
    mov [rbx], rax

    test r13, r13
    jnz .unlink_from_prev

    ; Removing the head
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
    ; mmap a new chunk and add it as a free block at head of list
    mov rdi, 0                      ; addr = NULL
    mov rsi, CHUNK_SIZE             ; length
    mov rdx, PROT_RW                ; prot
    mov r10, MAP_PRIV_ANON          ; flags
    mov r8, -1                      ; fd = -1
    mov r9, 0                       ; offset
    mov rax, SYS_MMAP
    syscall
    test rax, rax
    js .oom                         ; mmap failed

    ; Set up block header: size = CHUNK_SIZE (free, bit 0 = 0)
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
    pop r13
    pop r12
    pop rbx
    ret

; ============================================================
; void qk_std__heap__heap_free(ptr)
; rdi = pointer (or NULL)
; ============================================================
qk_std__heap__heap_free:
    test rdi, rdi
    jz .free_return                 ; free(NULL) is a no-op

    push rbx
    push r12

    ; Get block header
    lea rbx, [rdi - HEADER_SIZE]    ; rbx = this block

    ; Mark as free
    mov rax, [rbx]
    and rax, -2                     ; clear allocated flag
    mov [rbx], rax

    ; Try to coalesce with next physical block
    mov r12, [rbx]                  ; r12 = this block size (free)
    lea rcx, [rbx + r12]           ; rcx = next physical block

    ; Check if next block exists and is free
    mov rax, [rcx]
    test al, 1
    jnz .no_coalesce_next           ; next is allocated, skip

    ; Next block is free — remove it from free list and merge
    ; We need to find the block before 'next' in the free list
    ; to unlink it
    mov rdx, [heap_head]
    xor r8, r8                      ; r8 = previous block (NULL = head)

.find_next:
    cmp rdx, rcx
    je .found_next
    test rdx, rdx
    jz .no_coalesce_next            ; shouldn't happen, but be safe
    mov r8, rdx
    mov rdx, [rdx + 8]
    jmp .find_next

.found_next:
    ; Unlink next block from free list
    mov rax, [rcx + 8]              ; rax = next->next
    test r8, r8
    jnz .next_not_head
    mov [heap_head], rax
    jmp .merge_next
.next_not_head:
    mov [r8 + 8], rax

.merge_next:
    ; Merge: this_block.size += next_block.size
    mov rax, [rcx]                  ; next block size
    and rax, -2
    add [rbx], rax

.no_coalesce_next:
    ; Insert this block at head of free list
    mov rax, [heap_head]
    mov [rbx + 8], rax
    mov [heap_head], rbx

    pop r12
    pop rbx
.free_return:
    ret

; ============================================================
; void* qk_std__heap__heap_realloc(ptr, new_size)
; rdi = old pointer (or NULL)
; rsi = new size in bytes
; Returns: pointer, or NULL on OOM
; ============================================================
qk_std__heap__heap_realloc:
    ; realloc(NULL, size) -> malloc(size)
    test rdi, rdi
    jz qk_std__heap__heap_malloc

    ; realloc(ptr, 0) -> free(ptr), return NULL
    test rsi, rsi
    jnz .realloc_start
    push rdi
    call qk_std__heap__heap_free
    pop rdi
    xor eax, eax
    ret

.realloc_start:
    push rbx
    push r12
    push r13
    push r14

    mov r12, rdi                    ; r12 = old pointer
    lea rbx, [rdi - HEADER_SIZE]    ; rbx = old block header
    mov rax, [rbx]
    and rax, -2                     ; rax = old block size

    ; Calculate new aligned size
    lea rcx, [rsi + HEADER_SIZE + ALIGN_MASK]
    and rcx, -16
    cmp rcx, MIN_BLOCK
    jae .rsize_ok
    mov rcx, MIN_BLOCK
.rsize_ok:
    mov r13, rcx                    ; r13 = new block size needed

    ; If new size fits in old block, we're done (maybe shrink)
    cmp r13, rax
    ja .try_extend

    ; Shrink in place: optionally split off a free block from the end
    sub rax, r13
    cmp rax, MIN_BLOCK
    jb .realloc_done                ; not enough to split, keep it all

    ; Split: create a free block after the shrunken allocation
    lea r14, [rbx + r13]           ; r14 = new free block address
    mov [rbx], r13
    or byte [rbx], 1                ; mark old block as used (with new size)

    ; Set up free block
    mov [r14], rax                  ; free block size (flag = 0)

    ; Insert free block at head of free list (and coalesce with next)
    ; We'll just call free on the new block's user ptr
    push rdi                        ; save old pointer (r12)
    lea rdi, [r14 + HEADER_SIZE]
    call qk_std__heap__heap_free
    pop rdi

    mov rax, r12                    ; return original pointer
    jmp .realloc_done

.try_extend:
    ; Try to extend into the next physical block
    mov r14, [rbx]
    and r14, -2                     ; r14 = old block size
    lea rcx, [rbx + r14]           ; rcx = next physical block
    mov rdx, [rcx]
    test dl, 1                      ; is next block free?
    jnz .alloc_new                  ; nope, can't extend

    ; Next block is free. Check combined size.
    mov r8, [rcx]
    and r8, -2                      ; r8 = next block size
    mov rax, r14
    add rax, r8                     ; rax = combined size
    cmp rax, r13
    jb .alloc_new                   ; still not big enough

    ; We can extend! Remove next block from free list
    mov rdx, [heap_head]
    xor r9, r9
.find_next_ext:
    cmp rdx, rcx
    je .found_next_ext
    test rdx, rdx
    jz .alloc_new                   ; shouldn't happen
    mov r9, rdx
    mov rdx, [rdx + 8]
    jmp .find_next_ext

.found_next_ext:
    mov rax, [rcx + 8]
    test r9, r9
    jnz .next_ext_not_head
    mov [heap_head], rax
    jmp .merge_ext
.next_ext_not_head:
    mov [r9 + 8], rax

.merge_ext:
    ; Merge old block and next free block
    mov rax, [rcx]
    and rax, -2
    add rax, r14
    mov [rbx], rax
    or byte [rbx], 1                ; mark as used

    ; Now check if we need to shrink (combined may be larger than needed)
    mov rax, [rbx]
    and rax, -2
    sub rax, r13
    cmp rax, MIN_BLOCK
    jb .realloc_done                ; no split needed

    ; Split off a free block from the end
    lea rcx, [rbx + r13]
    mov [rbx], r13
    or byte [rbx], 1
    mov [rcx], rax                  ; free block

    push rdi
    lea rdi, [rcx + HEADER_SIZE]
    call qk_std__heap__heap_free
    pop rdi

    mov rax, r12
    jmp .realloc_done

.alloc_new:
    mov rdi, rsi                    ; rdi = new_size
    call qk_std__heap__heap_malloc
    test rax, rax
    jz .realloc_oom

    ; rdx = min(old_usable, new_usable)
    ;   old_usable = old_block_size - HEADER_SIZE
    ;   new_usable = r13 - HEADER_SIZE
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
    mov rdi, r12                    ; free old pointer
    call qk_std__heap__heap_free
    pop rax                         ; return new ptr
    jmp .realloc_done

.realloc_oom:
    xor eax, eax

.realloc_done:
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
