; Recursive sum(n) = n + sum(n-1): CALL/RET use R14 for the return address,
; so each level saves R14 (and n) with the stack macros.
; Expected: R1=5  R2=15 (sum of 1..5)  SP=4096

.main:
MOV R1, 5 ; R1 = n = 5
CALL sum
HALT

sum:
    ADD R2, R1, R0     ; R2 = n, sets Z when n == 0
    BEQ zero
    PUSH R14           ; save the return address across the recursive call
    PUSH R1            ; save n
    SUB R1, R1, 1
    CALL sum
    POP R1             ; R1 = saved n
    POP R14            ; restore the return address
    ADD R2, R2, R1     ; R2 = sum(n-1) + n
    RET
zero:
    MOV R1, 0      ; R1 = 0
    RET
