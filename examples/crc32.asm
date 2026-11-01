; CRC-32 (reflected, poly 0xEDB88320) of the ASCII string "123456789".
; The standard check value is 0xCBF43926 (= -873187034 as a signed word).
; Expected: R1=-873187034
.main:
ADD R3, R0, 0
ADD R4, R0, 0x31
init:
    STORE R4, 0[R3]
    ADD R4, R4, 1
    ADD R3, R3, 1
    SUB R5, R3, 9
    BEQ crcstart
    JMP init
crcstart:
    ADD R1, R0, -1
    ADD R2, R0, 0xEDB8
    SHL R2, R2, 16
    ADD R2, R2, 0x8320
    ADD R3, R0, 0
bytel:
    LOAD R4, 0[R3]
    XOR R1, R1, R4
    ADD R6, R0, 8
bitl:
    AND R5, R1, 1
    SHR R1, R1, 1
    ADD R5, R5, 0
    BEQ nox
    XOR R1, R1, R2
nox:
    SUB R6, R6, 1
    BEQ bitdone
    JMP bitl
bitdone:
    ADD R3, R3, 1
    SUB R4, R3, 9
    BEQ crcdone
    JMP bytel
crcdone:
    XOR R1, R1, -1
    HALT
