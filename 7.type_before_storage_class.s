    .data
    .align 4
bar.1:
    .long 4
    .text
foo.0:
    pushq    %rbp
    movq     %rsp, %rbp
    movl     $3, %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret
    movl     $0, %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret
    .globl main
    .text
main:
    pushq    %rbp
    movq     %rsp, %rbp
    subq     $16, %rsp
    call     foo.0
    movl     %eax, -4(%rbp)
    movl     -4(%rbp), %r10d
    movl     %r10d, -8(%rbp)
    movl     bar.1(%rip), %r10d
    addl     %r10d, -8(%rbp)
    movl     -8(%rbp), %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret
    movl     $0, %eax
    movq     %rbp, %rsp
    popq     %rbp
    ret

    .section .note.GNU-stack,"",@progbits
