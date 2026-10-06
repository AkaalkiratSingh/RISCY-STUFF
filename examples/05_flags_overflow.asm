; 05_flags_overflow.asm
; Builds 0x40000000 with a shift, then doubles it into signed overflow.
; Expected: R1=0x40000000 (1073741824)  R2=0x80000000 (-2147483648)
;           final FLAGS: Z=0 N=1 C=0 V=1

ADD R1, R0, 1
SHL R1, R1, 30
ADD R2, R1, R1
HALT
