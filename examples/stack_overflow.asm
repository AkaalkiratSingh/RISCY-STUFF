; Recursive CALL with no base case — the stack guard halts it.
; Expected: EXCEPTION: INVALID_MEMORY_ACCESS  (SP runs down from 4096)

.main:
loop:
CALL loop
