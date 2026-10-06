; 02_arithmetic_logic.asm
; Exercises every ALU op the control unit currently implements.
; Expected:
;   R3 = 12 - 10            = 2
;   R4 = 12 & 10            = 8
;   R5 = 12 | 10            = 14
;   R6 = 12 ^ 10            = 6
;   R7 = 12 << 2            = 48
;   R8 = 48 >> 1            = 24

ADD R1, R0, 12
ADD R2, R0, 10
SUB R3, R1, R2
AND R4, R1, R2
OR  R5, R1, R2
XOR R6, R1, R2
SHL R7, R1, 2
SHR R8, R7, 1
HALT
