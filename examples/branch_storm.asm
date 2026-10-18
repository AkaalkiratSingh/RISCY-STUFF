; Synthetic control-hazard workload: a chain of eight taken JMPs, repeated 100 times.
; Each taken transfer discards the instructions fetched behind it, so nearly all of
; this program's overhead shows up as flush bubbles rather than stalls. Note that
; this design flushes on every JMP, even when the target is the next instruction,
; so this is a worst case rather than a typical one.
; Expected: R1=100  and the flush-bubble count dominates the cycle total

ADD R1, R0, 0
loop:
    JMP b1
b1:
    JMP b2
b2:
    JMP b3
b3:
    JMP b4
b4:
    JMP b5
b5:
    JMP b6
b6:
    JMP b7
b7:
    JMP b8
b8:
    ADD R1, R1, 1
    SUB R2, R1, 100
    BEQ done
    JMP loop
done:
    HALT
