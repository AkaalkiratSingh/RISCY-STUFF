; Collatz trajectory of 27: n -> n/2 (even) or 3n+1 (odd) until n == 1.
; Tracks the step count and the peak value (signed compare via SHR 31).
; Expected: R2=111 (steps)  R3=9232 (peak)  R1=1

.main:
MOV R1, 27
MOV R2, 0
MOV R3, 27
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
    MOV R3, R1
    JMP step
even:
    SHR R1, R1, 1
step:
    ADD R2, R2, 1
    JMP loop
done:
    HALT
