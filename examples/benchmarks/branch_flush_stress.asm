; Control-hazard stress: 16384 alternating parity branches plus four taken jumps per
; iteration. The skipped paths contain a divide-by-zero and sentinel-corrupting stores.
; Expected: R1=16384  R2=R3=8192  R5=0  R6=MEM[0]=777  no exception
; Run with: risc201-sim examples/benchmarks/branch_flush_stress.asm -6 --no-forward

.main:
MOV R1, 0
MOV R2, 0
MOV R3, 0
MOV R9, 777
STORE R9, 0[R0]
loop:
    AND R4, R1, 1
    BEQ even
    ADD R3, R3, 1
    JMP join
even:
    ADD R2, R2, 1
join:
    JMP hop1
    STORE R0, 0[R0]
hop1:
    JMP hop2
    DIV R5, R9, 0
hop2:
    JMP hop3
    STORE R0, 0[R0]
hop3:
    JMP hop4
    DIV R5, R9, 0
hop4:
    ADD R1, R1, 1
    CMP R1, 16384
    BEQ done
    JMP loop
done:
    LOAD R6, 0[R0]
    HALT
