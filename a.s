    .globl main
main:
    pushq    %rbp
    movq     %rsp, %rbp
    subq     $12, %rsp
    movl     $2147483642, -4(%rbp)
.L1:
.L2:
    movl     -4(%rbp), %r10d
    movl     %r10d, -8(%rbp)
    subl     $5, -8(%rbp)
    movl     -8(%rbp), %r10d
    movl     %r10d, -4(%rbp)
    cmpl     $256, -4(%rbp)
    movl     $0, -12(%rbp)
    setge    -12(%rbp)
    movl     $0, %r11d
    cmpl     -12(%rbp), %r11d
    jne    .L1
.L3:
    movl     -4(%rbp), %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret
    movl     $0, %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret

    .section .note.GNU-stack,"",@progbits
