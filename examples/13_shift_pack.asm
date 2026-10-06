; 13_shift_pack.asm
; Packs 0x01 and 0x34 into 0x1340 using shifts.
; Expected: R1=4928 (0x1340)

ADD R1, R0, 0x01
SHL R1, R1, 8
ADD R1, R1, 0x34
SHL R1, R1, 4
HALT
