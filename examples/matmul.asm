; 8x8 integer matrix multiply, C = A * B, with a software shift-and-add multiplier.
; A is MEM[0..63] with A[i][k] = 8*i + k, B is MEM[64..127] and all ones, so every
; C[i][j] = sum_k(8*i + k) = 64*i + 28. Three nested loops over 512 multiplies make
; this the branch-heavy realistic workload. The address arithmetic spaces the two
; LOADs far enough apart that it takes no load-use stalls; pointer_walk is the
; control for that hazard.
; Run with: risc201-sim matmul.asm --mem 128:64
; Expected: MEM[128]=28  MEM[135]=28  MEM[191]=476

ADD R1, R0, 0
init:
    STORE R1, 0[R1]
    ADD R2, R0, 1
    ADD R3, R1, 64
    STORE R2, 0[R3]
    ADD R1, R1, 1
    SUB R3, R1, 64
    BEQ initdone
    JMP init
initdone:
    ADD R9, R0, 0
iloop:
    ADD R10, R0, 0
jloop:
    ADD R8, R0, 0
    ADD R11, R0, 0
kloop:
    SHL R5, R9, 3
    ADD R5, R5, R11
    LOAD R1, 0[R5]
    SHL R6, R11, 3
    ADD R6, R6, R10
    ADD R6, R6, 64
    LOAD R2, 0[R6]
    CALL mul
    ADD R8, R8, R3
    ADD R11, R11, 1
    SUB R7, R11, 8
    BEQ kdone
    JMP kloop
kdone:
    SHL R5, R9, 3
    ADD R5, R5, R10
    ADD R5, R5, 128
    STORE R8, 0[R5]
    ADD R10, R10, 1
    SUB R7, R10, 8
    BEQ jdone
    JMP jloop
jdone:
    ADD R9, R9, 1
    SUB R7, R9, 8
    BEQ done
    JMP iloop
done:
    HALT

; mul: R3 = R1 * R2, clobbers R1, R2, R4
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
