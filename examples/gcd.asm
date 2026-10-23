; Euclid's algorithm by repeated subtraction: gcd(1071, 462).
; Expected: R1=21

ADD R1, R0, 1071
ADD R2, R0, 462
loop:
    SUB R3, R1, R2
    BEQ done
    SHR R4, R3, 31
    BEQ a_bigger
    SUB R2, R2, R1
    JMP loop
a_bigger:
    ADD R1, R3, 0
    JMP loop
done:
    HALT