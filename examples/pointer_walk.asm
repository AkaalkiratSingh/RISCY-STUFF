; Synthetic load-use workload: 256 times, load a word and consume it on the very next
; instruction, then write the result back. A load's data does not exist until its
; MEM stage completes, so this is the one hazard forwarding cannot remove. Expect
; exactly one stall per iteration with forwarding on, and many more without it.
; Run with: risc201-sim pointer_walk.asm --mem 0:16
; Expected: MEM[0..255] = 1, stalls 256, flushes 256 (the loop's JMP)

.main:
ADD R2, R0, 0
loop:
    LOAD R3, 0[R2]
    ADD R4, R3, 1
    STORE R4, 0[R2]
    ADD R2, R2, 1
    SUB R5, R2, 256
    BEQ done
    JMP loop
done:
    HALT
