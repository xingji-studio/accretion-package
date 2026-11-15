.global _start
.type _start, @function
_start:
    endbr64
    andq $-16, %rsp
    call ap_xj380_main
    movq %rax, %rdi
    movq $60, %rax
    syscall
1:  jmp 1b
.size _start, . - _start
.section .note.GNU-stack,"",@progbits
