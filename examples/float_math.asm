; Float arithmetic in IEEE-754 single precision, held in ordinary 32-bit registers.
; Literals are preloaded into data memory at MEM[0], MEM[1], ... before execution.
; (5.0 + 3.0 - 2.0) * 0.5 / 2.0 = 1.5
; Expected: R3 = 1.5f (0x3FC00000 = 1069547520) and MEM[10] holds the same bits.

.main:
    LOAD R1, 0[R0]      ; 5.0
    LOAD R2, 1[R0]      ; 3.0
    FADD R3, R1, R2     ; 8.0
    LOAD R4, 2[R0]      ; 2.0
    FSUB R3, R3, R4     ; 6.0
    LOAD R5, 3[R0]      ; 0.5
    FMUL R3, R3, R5     ; 3.0
    LOAD R6, 2[R0]      ; 2.0
    FDIV R3, R3, R6     ; 1.5
    STORE R3, 10[R0]    ; keep the result in data memory
    HALT

.literals:
    five:  5.0f
    three: 3.0f
    two:   2.0f
    half:  0.5f
