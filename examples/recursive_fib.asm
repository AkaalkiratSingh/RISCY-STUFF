; Naive double recursion: fib(n) = fib(n-1) + fib(n-2). Argument and result in R1.
; Stresses CALL/RET depth, stack traffic and the RET flush depth. fib(15) makes
; 1973 calls, and each level's POP feeds the very next ADD, so it also takes one
; load-use stall per level.
; Expected: R1=610 (fib(15))  SP=4096

.main:
ADD R1, R0, 15
CALL fib
HALT

fib:
    SUB R2, R1, 2
    SHR R2, R2, 31
    BEQ recurse
    RET
recurse:
    PUSH R1
    SUB R1, R1, 1
    CALL fib
    ADD R8, R1, 0
    POP R1
    PUSH R8
    SUB R1, R1, 2
    CALL fib
    POP R8
    ADD R1, R1, R8
    RET
