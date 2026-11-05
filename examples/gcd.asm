; Euclid's algorithm by repeated subtraction: gcd(1071, 462).
; Expected: R1=21

.main:
MOV R1, 1071
MOV R2, 462
loop:
    SUB R3, R1, R2
    BEQ done
    SHR R4, R3, 31
    BEQ a_bigger
    SUB R2, R2, R1
    JMP loop
a_bigger:
    MOV R1, R3
    JMP loop
done:
    HALT
