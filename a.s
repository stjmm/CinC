    .globl main
main:
    pushq    %rbp
    movq     %rsp, %rbp
    subq     $4, %rsp
    movl     $0, %r11d
    cmpl     $1, %r11d
    je    .L0
    movl     $0, %r11d
    cmpl     $0, %r11d
    je    .L0
    movl     $1, %r10d
    movl     %r10d, -4(%rbp)
    jmp    .L1
.L0:
    movl     $0, %r10d
    movl     %r10d, -4(%rbp)
.L1:
    movl     -4(%rbp), %r10d
    movl     %r10d, %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret

    .section .note.GNU-stack,"",@progbits
