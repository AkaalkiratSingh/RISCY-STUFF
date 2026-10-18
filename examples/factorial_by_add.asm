; 12! built on a software shift-and-add multiplier (no MUL instruction needed).
; mul: R3 = R1 * R2   (clobbers R1, R2)
; Expected: R5=479001600

ADD R5, R0, 1
ADD R6, R0, 2
fact:
    ADD R1, R5, 0
    ADD R2, R6, 0
    CALL mul
    ADD R5, R3, 0
    ADD R6, R6, 1
    SUB R7, R6, 13
    BEQ fdone
    JMP fact
fdone:
    HALT

mul:
    ADD R3, R0, 0
mloop:
    ADD R4, R2, 0
    BEQ mdone
    AND R4, R2, 1
    BEQ skip
    ADD R3, R3, R1
skip:
    SHL R1, R1, 1
    SHR R2, R2, 1
    JMP mloop
mdone:
    RET