; Memory-traffic stress: 32 sweeps over two 1024-word buffers, then a checksum.
; Expected: R9=589312  MEM[i]=i+64 and MEM[1024+i]=i+63 for 0<=i<1024
; Run with: risc201-sim examples/benchmarks/memory_ping_pong.asm -4 -m 0:8

.main:
MOV R1, 0
init:
    STORE R1, 0[R1]
    ADD R1, R1, 1
    CMP R1, 1024
    BEQ ready
    JMP init
ready:
    MOV R8, 32
pass:
    MOV R1, 0
sweep:
    LOAD R2, 0[R1]
    ADD R2, R2, 1
    STORE R2, 1024[R1]
    LOAD R3, 1024[R1]
    ADD R3, R3, 1
    STORE R3, 0[R1]
    ADD R1, R1, 1
    CMP R1, 1024
    BEQ pass_done
    JMP sweep
pass_done:
    SUB R8, R8, 1
    BEQ checksum_start
    JMP pass
checksum_start:
    MOV R1, 0
    MOV R9, 0
checksum:
    LOAD R2, 0[R1]
    ADD R9, R9, R2
    ADD R1, R1, 1
    CMP R1, 1024
    BEQ done
    JMP checksum
done:
    HALT
