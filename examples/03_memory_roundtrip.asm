; 03_memory_roundtrip.asm
; Stores a register to data memory and loads it back.
; Data memory starts at zero, so the STORE is what makes this meaningful.
; Run with: risc201-sim 03_memory_roundtrip.asm --mem 0:8
; Expected: R1=42  R2=42  R3=43  and MEM[4]=42

ADD R1, R0, 42
STORE R1, 4[R0]
LOAD R2, 4[R0]
ADD R3, R2, 1
HALT
