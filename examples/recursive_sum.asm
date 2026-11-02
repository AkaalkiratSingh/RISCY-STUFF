; Recursive sum(n) = n + sum(n-1): CALL/RET for recursion, PUSH/POP to keep n.
; Expected: R1=5  R2=15 (sum of 1..5)  SP=4096

.main:
ADD R1, R0, 5 ; R1 = n = 5
CALL sum
HALT

sum:
    ADD R2, R1, R0     ; R2 = n, sets Z when n == 0
    BEQ zero
    PUSH R1            ; save n across the recursive call
    SUB R1, R1, 1
    CALL sum
    POP R1             ; R1 = saved n
    ADD R2, R2, R1     ; R2 = sum(n-1) + n
    RET
zero:
    ADD R1, R0, 0      ; R1 = 0
    RET
