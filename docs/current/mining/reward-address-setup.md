# Mining Reward Address Setup Guide

**Version:** LLL-TAO 5.1.0+  
**Updated:** 2026-03-25

---

## Overview

Before a Nexus mining node will issue a block template, the miner must bind a **reward address** using the `MINER_SET_REWARD` protocol opcode.

The reward address **must** be a **Tritium GenesisHash** — the 32-byte sigchain owner hash. Any other type of address is rejected by the node and will result in **no NXS reward**, even if the proof-of-work is valid.

---

## What Is a Tritium GenesisHash?

A Tritium GenesisHash is the unique identity hash of a Nexus user account (sigchain). It is:

- A **32-byte hash** derived from the user's credentials
- Permanently stored on-chain when the user creates their first transaction
- Visible as the **"Owner"** field on the Nexus Explorer and in the Nexus Interface
- Used by the consensus layer to credit mining rewards

### Format

| Field | Value |
|-------|-------|
| Size | 32 bytes (64 hex characters) |
| Leading type byte (mainnet) | `0xa1` |
| Leading type byte (testnet) | `0xb1` |

**Example (mainnet):**
```
a174011c93ca1c80bca5388382b167cacd33d3154395ea8f45ac99a8308cd122
^^
└─ 0xa1 = mainnet GenesisHash type byte
```

---

## How to Find Your GenesisHash

### Method 1: Nexus Interface (Recommended)

1. Open the Nexus wallet application
2. Navigate to **Profile** (or **Account**)
3. Look for the **Owner** field
4. Copy the 64-character hex string — that is your GenesisHash

### Method 2: Nexus Explorer

1. Go to https://explorer.nexus.io
2. In the search box, enter your **username**
3. On the profile page, find the **"Owner"** field
4. Copy the 64-character hex string

---

## Consensus Requirement

Upstream Nexus consensus (`TAO::Operation::Coinbase::Verify()`) enforces:

```cpp
// src/TAO/Operation/coinbase.cpp
if(hashGenesis.GetType() != TAO::Ledger::GENESIS::UserType())
    return debug::error(FUNCTION, "invalid genesis for coinbase");
```

`GENESIS::UserType()` returns:
- `0xa1` on mainnet
- `0xb1` on testnet

**Any other leading byte causes the block to be rejected or produces no reward credit.**

---

## What Happens at `MINER_SET_REWARD`

When `MINER_SET_REWARD` is received, the node performs these checks in order:

| Check | Failure Result |
|-------|---------------|
| Address is not zero | `{0x00}` — zero address rejected |
| `IsValidGenesisType()` — type byte matches `UserType()` | `{0x00}` — wrong type byte |
| `ExistsOnChain()` — genesis has at least one transaction | `{0x00}` — not found on-chain |
| ✅ All pass | `{0x01}` — reward address bound |

A `{0x01}` response confirms the reward address is valid. Mining can begin once `SET_CHANNEL` and `MINER_READY` are also sent.

---

## Common Mistakes

### ❌ Using an Account Register Address

Account register addresses start with type bytes `0xd1`–`0xed`. These are **not** GenesisHash addresses.

```
d1a403c8b3f61c92fe7e12d0...  ← WRONG — account register address
a174011c93ca1c80bca53883...  ← CORRECT — GenesisHash
```

**What happens:** `MINER_SET_REWARD` returns `{0x00}` (invalid type byte). No blocks are issued.

### ❌ Using a Random Hash

Any 32-byte hash that does not have the `UserType()` leading byte will fail the type check, and any hash that has never been committed on-chain will fail the existence check.

### ❌ Not Sending MINER_SET_REWARD at All

If `GET_BLOCK` is called before `MINER_SET_REWARD`, the node will reject the request:
```
GET_BLOCK: reward address not set - send MINER_SET_REWARD first
```

---

## Pool Mining

For pool operations, the pool provides **its own GenesisHash** as the reward address. The pool receives all on-chain rewards to its sigchain and distributes them to miners internally based on shares.

Individual miners authenticate using their own Falcon key pair via `MINER_AUTH_INIT` / `MINER_AUTH_RESPONSE`, but the reward address (`MINER_SET_REWARD`) is set by the pool. This design is fully compatible with the GenesisHash-only requirement.

---

## Troubleshooting: "Block accepted but no reward"

If the Nexus network accepts a block but no NXS appears in the wallet:

1. **Check for `invalid genesis for coinbase` in node logs** — this means the reward address reached the consensus layer with the wrong type byte (should have been caught at `MINER_SET_REWARD` in updated nodes)
2. **Verify the reward address starts with `a1` (mainnet) or `b1` (testnet)**
3. **Confirm your sigchain has at least one on-chain transaction** — a fresh sigchain with no transactions passes the type check but fails `ExistsOnChain`
4. **Check for `AUTO-CREDIT` log lines** — if `SetBest` fires but `AUTO-CREDIT` does not appear, the coinbase verification failed silently

### Log Examples

**Valid GenesisHash accepted:**
```
[FUNCTION] Reward GenesisHash validated: a174011c93ca1c80...
[FUNCTION] REWARD BINDING DIAGNOSTIC
[FUNCTION] - decoded reward GenesisHash: a174011c93ca1c80...
```

**Wrong type byte rejected:**
```
[FUNCTION] Reward address is not a valid Tritium GenesisHash (type byte 0xd1 != expected 0xa1)
[FUNCTION] Reward address MUST be a Tritium GenesisHash (sigchain owner).
[FUNCTION] Find yours on https://explorer.nexus.io (Owner field) or in the Nexus Interface.
```

**Not on-chain rejected:**
```
[FUNCTION] Reward GenesisHash not found on chain: a174011c93ca1c80...
[FUNCTION] Make sure your sigchain has at least one transaction on the Nexus network.
```

---

## See Also

- [Stateless Mining Protocol](stateless-protocol.md) — full protocol documentation
- [Mining Server Architecture](mining-server.md) — server internals
- [Opcodes Reference](../../reference/opcodes-reference.md) — `MINER_SET_REWARD` opcode details
- https://explorer.nexus.io — find your GenesisHash online
