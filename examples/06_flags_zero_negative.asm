; 06_flags_zero_negative.asm
; 9 - 9 sets the zero flag; 0 - 9 shows the sign/borrow behaviour.
; The last instruction sets the flags that remain in the register file.
; Expected: R1=9  R3=-9  R2=0  (final FLAGS: Z=1 N=0 C=1 V=0)

ADD R1, R0, 9
SUB R3, R0, R1
SUB R2, R1, R1
HALT
