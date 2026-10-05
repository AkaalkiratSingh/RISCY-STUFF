; 08_conditional_branch.asm
; SUB sets Z (3 - 3 = 0); the BEQ skips the fall-through block.
; Expected: R1=3  R2=0  R3=0 (skipped)  R4=1  PC=5

ADD R1, R0, 3
SUB R2, R1, 3
BEQ equal
ADD R3, R0, 111
equal:
ADD R4, R0, 1
HALT
