; 10_multiply.asm
; 3 * 4 by repeated addition; the counter drives the loop exit.
; Expected: R3=12  R2=0

ADD R1, R0, 3
ADD R2, R0, 4
ADD R3, R0, 0
loop:
ADD R3, R3, R1
SUB R2, R2, 1
BEQ done
JMP loop
done:
HALT
