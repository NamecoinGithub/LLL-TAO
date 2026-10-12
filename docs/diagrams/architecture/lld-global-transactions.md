# LLD global transaction ownership

NODE5 does not use a cross-mode coordinator. `pMiner`, `pSanitize`, and the physical journal are process-wide, so MINER, SANITIZE, and the physical journal each have an owner slot. MINER and SANITIZE can stay open beside MEMPOOL. A slot mutex is held only across that slot's mutation. Shared-overlay checks lock the MEMPOOL slot before the physical slot.

`MemoryBegin` maps every flag other than MINER and SANITIZE onto the same process-wide `pMemory`. That includes `FLAGS::MEMPOOL` and physical `FLAGS::BLOCK`. Those two modes therefore share one overlay even though they have separate owner slots. `TxnBegin` rejects the second begin, whether it comes from another thread or the same thread, before `MemoryBegin`. A duplicate begin of a slot that is already owned, including by the calling thread, is rejected the same way. A rejected begin does not replace the overlay or open the physical journal.

`TritiumBlock::Accept()` must not call `mempool.ProcessOrphans(hashProducer)` while its physical `BLOCK` guard is armed. That routine accepts each orphan through `TxnBegin(FLAGS::MEMPOOL)`, which this rule rejects, so the orphan would stay queued. Producer writes stay in the open block transaction through `Index()` / `SetBest()`. After that transaction is committed and `TransactionGuard::Release()` has run, Accept processes producer orphans only when disk `ReadLast` for the producer genesis is the producer. A stored side chain has not connected, so its orphans stay queued instead of being recorded as a false conflict.

```mermaid
flowchart TD
    A["TxnBegin(mode)"] --> B{"This slot already owned?"}
    B -->|yes, this thread or another| C["Reject. Do not replace the overlay or journal"]
    B -->|no| S{"MEMPOOL or BLOCK, and the other shared slot is owned?"}
    S -->|yes| C
    S -->|no| D["Claim slot, then MemoryBegin or journal TxnBegin"]
    D --> Q{"A started call throws?"}
    Q -->|yes| R["Release finished instances and the claimed slot, then propagate"]
    Q -->|no| T["Begin returned. Caller owns the slot"]

    E["TxnAbort / TxnCommit(mode)"] --> F{"This thread owns that slot?"}
    F -->|memory abort or MEMPOOL, no| G["Reject. Leave overlay and owner unchanged"]
    F -->|MINER or SANITIZE, foreign owner| K["Reject. Do not release that owner"]
    F -->|memory abort, MEMPOOL commit, or physical| M{"Mask covers instances opened by begin?"}
    M -->|no| N["Reject. Leave owner, journals, and overlays unchanged"]
    M -->|yes| H["Mutate those instances, then release only that owner"]
    F -->|MINER or SANITIZE commit| P["No-mutation short-circuit. Do not release that owner"]
    F -->|physical, other owner| I["Reject before TxnRelease or TxnCommit"]
    F -->|physical, unowned| J["No-op journal path. Skip pMemory if another thread owns MEMPOOL"]
```

MINER and SANITIZE `TxnCommit` stays a no-mutation short-circuit. It returns true when this thread owns that mode or owns nothing. It returns false when another thread owns that slot (a foreign owner), and when this thread owns a different mode but not this slot. Neither rejection releases an owner.

A physical commit applies the BLOCK-created `pMemory` only when this thread owns the physical slot and no other thread owns MEMPOOL. It does not commit or delete an overlay owned by another thread. Begin-time rejection is what keeps those two owners from being created together.

The stored begin mask is part of that ownership. An owned memory abort, a MEMPOOL commit, and an owned physical commit or abort must cover every instance that begin opened. A narrower mask is rejected before `MemoryCommit`, `MemoryRelease`, `TxnCommit`, or `TxnRelease`, and the owner stays claimed. A wider mask is not a subset: it still applies to every opened instance, which is what lets a commit aggregate an extra instance that was never begun. MINER and SANITIZE commit stay a no-mutation short-circuit and do not release that owner, so a narrower mask there cannot drop the slot. An unowned physical abort does not use this check; that path is still recovery.

If `MemoryBegin` or a selected journal `TxnBegin` throws after the slot is claimed, `TxnBegin` releases only the instances whose begin call returned, then clears that slot, and then propagates the exception. The throwing call is not released: those begin methods allocate the replacement before deleting the previous pointer, so a failed allocation leaves that instance unchanged. `TxnRelease` is not applied to a journal this begin did not open, because that call truncates `journal.dat`. A caller catch runs only after this rollback. It must not abort a begin that did not return, and a flag set only after `TxnBegin` returns cannot see the failed begin.

Disposable SANITIZE and MINER work uses `TransactionGuard` so the overlay is aborted if the thread still owns it when the guard is destroyed. That includes `Notifications::SanitizeContract`, each `SanitizeUnconfirmed` transaction, `Mempool::Check`'s sanitize probe, `CheckContract`, and the Tritium half of `AddTransactions`. `AddTransactions` disarms the guard and aborts before the legacy `Connect(FLAGS::MINER)` loop, so that loop does not run inside the Tritium overlay. An exception from `Verify` or `Connect` skips the disarm and the guard aborts on unwind. `Mempool::Check` ends the sanitize guard before it begins the MEMPOOL disconnect transaction. That disconnect still uses an explicit abort only when `Disconnect` fails, then `Remove`. A later `Delete` throw is not aborted by a guard: aborting after `Remove` would restore overlay effects for entries already erased from the pool maps.
