# LLD global transaction ownership

NODE5 does not use a cross-mode coordinator. `pMemory`, `pMiner`, `pSanitize`, and the physical journal are process-wide, so each mode has its own owner slot. MINER, SANITIZE, and MEMPOOL can stay open together. A slot mutex is held only across that slot's mutation.

```mermaid
flowchart TD
    A["TxnBegin(mode)"] --> B{"Slot owned by another thread?"}
    B -->|yes| C["Reject. Do not replace the overlay or journal"]
    B -->|no| D["Claim slot, then MemoryBegin or TxnBegin"]

    E["TxnAbort / TxnCommit(mode)"] --> F{"This thread owns that slot?"}
    F -->|memory mode, no| G["Reject. Leave overlay and owner unchanged"]
    F -->|yes| H["Mutate that slot, then release only that owner"]
    F -->|physical, other owner| I["Reject before TxnRelease or TxnCommit"]
    F -->|physical, unowned| J["No-op journal path. Skip pMemory if another thread owns MEMPOOL"]
```

MINER and SANITIZE `TxnCommit` stays a no-mutation short-circuit. It returns true when this thread owns that mode or owns nothing. It returns false when this thread owns a different mode, and it does not release that owner.

A physical commit applies `pMemory` only when this thread owns the MEMPOOL slot. It does not commit or delete an overlay owned by another thread.
