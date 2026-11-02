; Recursive CALL with no base case. CALL/RET use R14 for the return address, so
; each level pushes its saved R14; the stack eventually runs below memory and the
; store faults.
; Expected: EXCEPTION: INVALID_MEMORY_ACCESS

.main:
    CALL recurse
    HALT

recurse:
    PUSH R14
    CALL recurse
    POP R14
    RET
