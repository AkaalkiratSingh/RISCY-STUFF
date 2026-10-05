; 01_add_immediates.asm
; Adds two immediates through a register, then combines the registers.
; Expected: R1=5  R2=7  R3=12  (final FLAGS: Z=0 N=0 C=0 V=0)

ADD R1, R0, 5
ADD R2, R0, 7
ADD R3, R1, R2
HALT
