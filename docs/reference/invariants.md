# Hard-Won Invariants

Durable consensus/ledger/mining invariants that must not be violated.

## 1) Oracle Consistency

- **Rule:** A pre-check must use the same state oracle as the authoritative operation it predicts.
- **Rationale:** Mixed-oracle checks create state skew.
- **Failure mode:** False stale rejects or false accepts under concurrent mempool/session activity.
- **Sources:** `src/TAO/Ledger/stateless_block_utility.cpp`, `src/TAO/Ledger/state.cpp`, [sigchain-last-resolution.md](sigchain-last-resolution.md)

## 2) Merkle-Root Immutability Post-Sign

- **Rule:** After a miner finds a valid nonce, block fields covered by proof (`hashMerkleRoot`, producer, `vtx`, signature context) must not be mutated.
- **Rationale:** `ProofHash()` commits to `hashMerkleRoot`; mutation after sign/proof breaks proof validity.
- **Failure mode:** Valid solved blocks become invalid at submit/verify time.
- **Sources:** `src/TAO/Ledger/types/block.cpp`, `src/TAO/Ledger/stateless_block_utility.cpp`

## 3) Sigchain Ordering

- **Rule:** `tx.hashPrevTx` must equal the genesis last tx; `tx.hashPrevTx == tx.GetHash()` is malformed and must reject.
- **Rationale:** Sigchain entries are strictly linked by predecessor hash.
- **Failure mode:** Broken lineage, stale/malformed tx acceptance, downstream connect failures.
- **Sources:** `src/TAO/Ledger/stateless_block_utility.cpp`, `src/TAO/Ledger/types/transaction.cpp`

## 4) Disk-Only Connect Anchor

- **Rule:** `BlockState::Connect()` validates sigchain `last` against disk (`FLAGS::BLOCK`) as canonical committed state.
- **Rationale:** Connect-time consensus must be anchored to committed chain data.
- **Failure mode:** Pre-check/connect disagreement and non-deterministic block acceptance behavior.
- **Sources:** `src/TAO/Ledger/state.cpp`, `src/LLD/ledger.cpp`

## Extending This Index

Add future invariants as new numbered entries with: **rule**, **rationale**, **failure mode**, and **source references**.
