    .globl main
main:
    pushq    %rbp
    movq     %rsp, %rbp
    subq     $4, %rsp
    movl     $2, %r10d
    movl     %r10d, -4(%rbp)
    notl     -4(%rbp)
    movl     -4(%rbp), %r10d
    movl     %r10d, %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret

    .section .note.GNU-stack,"",@progbits
