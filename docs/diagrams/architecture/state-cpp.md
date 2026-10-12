# State.cpp

`src/TAO/Ledger/state.cpp` implements `BlockState::SetBest()`. Callers that already hold a physical `FLAGS::BLOCK` transaction (`TritiumBlock::Accept()`, `LegacyBlock::Accept()`, and checkpoint rewind) keep that outer owner. A standalone caller has no journal, so `SetBest()` opens one and must release it on every exit, including a throw from `Connect()` or `Disconnect()`.

## Self-owned versus outer transaction

```mermaid
flowchart TD
    A["SetBest()"] --> B{"HasOpenTransaction and this thread owns PhysicalSlot?"}
    B -->|open, foreign owner| C["Return false. Do not write or TxnAbort"]
    B -->|open, this thread| D["Outer transaction. fOwnedTxn = false"]
    B -->|not open| E["TxnBegin. fOwnedTxn = true"]
    E --> F{"Begin failed?"}
    F -->|yes| G["Return false. No slot claimed"]
    F -->|no| H["SelfOwnedTxnGuard armed"]
    D --> I["Disconnect and connect on disk"]
    H --> I
    I --> J{"Throw before commit?"}
    J -->|yes, self-owned| K["Guard TxnAbort(BLOCK, CONSENSUS)"]
    J -->|yes, outer| L["Propagate. Caller guard aborts its own slot"]
    J -->|no| M["TxnCommit"]
    M --> N["Release the self-owned guard"]
    N --> O["Publish ChainState and mempool only after commit"]
```

The guard checks `ThreadOwnsTransaction(FLAGS::BLOCK)` before aborting. After a successful commit the physical slot is already released, so a later post-commit throw does not abort a transaction `SetBest()` no longer owns. An outer transaction is never aborted by this guard.

Sibling documents: [Process.cpp](process-cpp.md), [Tritium.cpp](tritium-cpp.md), [LLD global transactions](lld-global-transactions.md).
