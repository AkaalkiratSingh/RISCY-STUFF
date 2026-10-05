; 11_fibonacci.asm
; Eight iterations of (a, b) <- (b, a + b), carried in registers.
; Expected: R1=21  R2=34  R3=0

ADD R1, R0, 0
ADD R2, R0, 1
ADD R3, R0, 8

loop:
    ADD R4, R1, R2
    ADD R1, R2, R0
    ADD R2, R4, R0
    SUB R3, R3, 1
    
BEQ done
JMP loop
done:
    HALT
