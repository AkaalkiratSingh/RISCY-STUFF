; Sieve of Eratosthenes below 100. MEM[k] = 1 marks k as composite.
; Run with: risc201-sim sieve.asm --mem 0:100
; Expected: R8=25 (primes below 100)

.main:
MOV R1, 2
outer:
    SUB R2, R1, 10
    BEQ count
    LOAD R3, 0[R1]
    ADD R3, R3, 0
    BEQ prime_i
    JMP next
prime_i:
    MOV R4, 0
    MOV R5, R1
sq:
    ADD R4, R4, R1
    SUB R5, R5, 1
    BEQ marking
    JMP sq
marking:
    SUB R6, R4, 100
    SHR R6, R6, 31
    BEQ next
    MOV R7, 1
    STORE R7, 0[R4]
    ADD R4, R4, R1
    JMP marking
next:
    ADD R1, R1, 1
    JMP outer
count:
    MOV R8, 0
    MOV R1, 2
cloop:
    SUB R2, R1, 100
    BEQ done
    LOAD R3, 0[R1]
    ADD R3, R3, 0
    BEQ isprime
    JMP cnext
isprime:
    ADD R8, R8, 1
cnext:
    ADD R1, R1, 1
    JMP cloop
done:
    HALT
