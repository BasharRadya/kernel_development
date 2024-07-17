    .section .data
errmsg_write:
    .ascii "write error\n\0"
errmsg_kdlp:
    .ascii "kdlp failed\n\0"
newline:
    .ascii "\n\0"

    .section .bss
    .lcomm buffer, 36

    .section .text
    .global _start

_start:
    leaq buffer(%rip), %rdi
    movq $36, %rsi
    movq $452, %rax
    syscall

    testq %rax, %rax
    js kdlp_failed

    movq $1, %rdi
    leaq buffer(%rip), %rsi
    movq %rax, %rdx
    movq $1, %rax
    syscall

    testq %rax, %rax
    js write_failed

    movq $1, %rdi
    leaq newline(%rip), %rsi
    movq $1, %rdx
    movq $1, %rax
    syscall

    jmp good_exit

write_failed:
    movq $2, %rdi
    leaq errmsg_write(%rip), %rsi
    movq $12, %rdx
    movq $1, %rax
    syscall
    jmp bad_exit

kdlp_failed:
    movq $2, %rdi
    leaq errmsg_kdlp(%rip), %rsi
    movq $12, %rdx
    movq $1, %rax
    syscall
    jmp bad_exit

good_exit:
    movq $0, %rdi
    jmp exit

bad_exit:
    movq $1, %rdi

exit:
    movq $60, %rax
    syscall
