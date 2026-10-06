# 04_branch_skip.asm
# Unconditional jump over a block that must not execute.
# Expected: R1=1  R2=0 (skipped)  R3=2  PC=4

ADD R1, R0, 1
JMP done
ADD R2, R0, 99

done:
    ADD R3, R0, 2
HALT
