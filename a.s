    .globl main
main:
    pushq    %rbp
    movq     %rsp, %rbp
    subq     $12, %rsp
    movl     $2, %r10d
    movl     %r10d, -4(%rbp)
    notl     -4(%rbp)
    movl     $3, %r10d
    movl     %r10d, -8(%rbp)
    movl     -8(%rbp), %r11d
    imull     $4, %r11d
    movl     %r11d, -8(%rbp)
    movl     -4(%rbp), %r10d
    movl     %r10d, -12(%rbp)
    movl     -4(%rbp), %r10d
    addl     %r10d, -12(%rbp)
    movl     -4(%rbp), %r10d
    movl     %r10d, %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret

    .section .note.GNU-stack,"",@progbits
