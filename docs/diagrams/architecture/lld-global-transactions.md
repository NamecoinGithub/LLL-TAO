# LLD global transaction ownership

NODE5 does not use a cross-mode coordinator. `pMiner`, `pSanitize`, and the physical journal are process-wide, so MINER, SANITIZE, and the physical journal each have an owner slot. MINER and SANITIZE can stay open beside MEMPOOL. A slot mutex is held only across that slot's mutation. Shared-overlay checks lock the MEMPOOL slot before the physical slot.

`MemoryBegin` maps every flag other than MINER and SANITIZE onto the same process-wide `pMemory`. That includes `FLAGS::MEMPOOL` and physical `FLAGS::BLOCK`. Those two modes therefore share one overlay even though they have separate owner slots. `TxnBegin` rejects the second begin, whether it comes from another thread or the same thread, before `MemoryBegin`. A duplicate begin of a slot that is already owned, including by the calling thread, is rejected the same way. A rejected begin does not replace the overlay or open the physical journal.

```mermaid
flowchart TD
    A["TxnBegin(mode)"] --> B{"This slot already owned?"}
    B -->|yes, this thread or another| C["Reject. Do not replace the overlay or journal"]
    B -->|no| S{"MEMPOOL or BLOCK, and the other shared slot is owned?"}
    S -->|yes| C
    S -->|no| D["Claim slot, then MemoryBegin or TxnBegin"]

    E["TxnAbort / TxnCommit(mode)"] --> F{"This thread owns that slot?"}
    F -->|memory abort or MEMPOOL, no| G["Reject. Leave overlay and owner unchanged"]
    F -->|MINER or SANITIZE, foreign owner| K["Reject. Do not release that owner"]
    F -->|yes| H["Mutate that slot, then release only that owner"]
    F -->|physical, other owner| I["Reject before TxnRelease or TxnCommit"]
    F -->|physical, unowned| J["No-op journal path. Skip pMemory if another thread owns MEMPOOL"]
```

MINER and SANITIZE `TxnCommit` stays a no-mutation short-circuit. It returns true when this thread owns that mode or owns nothing. It returns false when another thread owns that slot (a foreign owner), and when this thread owns a different mode but not this slot. Neither rejection releases an owner.

A physical commit applies the BLOCK-created `pMemory` only when this thread owns the physical slot and no other thread owns MEMPOOL. It does not commit or delete an overlay owned by another thread. Begin-time rejection is what keeps those two owners from being created together.
