; 09_countdown_loop.asm
; Counts R1 down to zero; BEQ exits when SUB makes the result zero.
; Expected: R1=0  retired=15

ADD R1, R0, 5
loop:
SUB R1, R1, 1
BEQ done
JMP loop
done:
HALT
