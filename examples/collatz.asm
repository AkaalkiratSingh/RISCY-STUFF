; Collatz trajectory of 27: n -> n/2 (even) or 3n+1 (odd) until n == 1.
; Tracks the step count and the peak value (signed compare via SHR 31).
; Expected: R2=111 (steps)  R3=9232 (peak)  R1=1

.main:
ADD R1, R0, 27
ADD R2, R0, 0
ADD R3, R0, 27
loop:
    SUB R4, R1, 1
    BEQ done
    AND R4, R1, 1
    BEQ even
    ADD R5, R1, R1
    ADD R1, R5, R1
    ADD R1, R1, 1
    SUB R4, R3, R1
    SHR R4, R4, 31
    BEQ step
    ADD R3, R1, 0
    JMP step
even:
    SHR R1, R1, 1
step:
    ADD R2, R2, 1
    JMP loop
done:
    HALT
