# CHACHA20_EVP_MANAGER — Architecture Reference

## Overview

`Chacha20EvpManager` is the node-side transport encryption gate for the Nexus mining protocol.
It is a singleton that owns the **EVP vs TLS mode decision** for the entire running node,
covering both the **Legacy Lane (port 8323)** and the **Stateless Lane (port 9323)**.

---

## Design Principles

1. **Singleton — one decision, node-wide.** Both lanes query the same manager. There is no
   per-lane override. A node is either EVP, TLS, or (for local testing only) NONE.

2. **EVP is the default.** ChaCha20-Poly1305 via OpenSSL EVP APIs is active unless
   `-miningtls` is explicitly set (and `-miningevp` is not set).

3. **EVP and TLS are mutually exclusive.** A node cannot run EVP on one lane and TLS on
   another. The manager enforces this at startup and at every runtime query.

4. **Non-invasive.** The manager wraps, not replaces, the existing LLC helpers
   (`LLC::EncryptPayloadChaCha20` / `LLC::DecryptPayloadChaCha20`). Existing direct call
   sites remain valid and are annotated to point at this manager as the future unified gate.

---

## EVP/TLS Mutual Exclusion Contract

```
-miningevp=1  -miningtls=0  →  EVP mode (default, recommended)
-miningevp=0  -miningtls=1  →  TLS mode (future; not yet implemented)
-miningevp=1  -miningtls=1  →  EVP wins, WARNING logged
-miningevp=0  -miningtls=0  →  EVP mode (EVP is always the default fallback)
```

The manager sets mode **once** at `Initialize()` and the atomic is never written again.
All subsequent reads are lock-free.

---

## How `Chacha20EvpManager` Wraps the LLC Helpers

```
Mining handler (SUBMIT_BLOCK, MINER_SET_REWARD, …)
        │
        ▼
Chacha20EvpManager::Get().Encrypt() / Decrypt()
        │
        │  [mode == EVP]
        ▼
LLC::EncryptPayloadChaCha20 / LLC::DecryptPayloadChaCha20
        │
        ▼
LLC::EncryptChaCha20Poly1305 (OpenSSL EVP_AEAD_CTX)
```

The LLC helpers are **not modified**. The manager is additive: it owns the mode decision and
routes to the correct implementation. Callers that already invoke the LLC helpers directly
remain valid.

---

## `AllowPlaintext()` — Localhost Exemption

```cpp
bool AllowPlaintext(const std::string& strPeerIP) const;
```

Returns `true` **only** when:
- `strPeerIP == "127.0.0.1"` (exact match)
- **AND** `IsEncryptionRequired() == false`

In EVP mode `IsEncryptionRequired()` returns `true`, so `AllowPlaintext()` always returns
`false` for all peers including localhost. This prevents accidental unencrypted submissions
from local test miners while the node is running in production mode.

| Mode | `IsEncryptionRequired()` | `AllowPlaintext("127.0.0.1")` | `AllowPlaintext("1.2.3.4")` |
|------|--------------------------|-------------------------------|------------------------------|
| EVP  | `true`                   | `false`                       | `false`                      |
| TLS  | `false`                  | `true`                        | `false`                      |
| NONE | `false`                  | `true`                        | `false`                      |

---

## Config Args

| Argument        | Default | Purpose                                          |
|-----------------|---------|--------------------------------------------------|
| `-miningevp`    | `true`  | Enable ChaCha20-Poly1305 EVP mode                |
| `-miningtls`    | `false` | Enable TLS mode (future, opt-in)                 |

**Conflict resolution:** if both are set, EVP wins and a `WARNING` is emitted to the node log.

---

## Startup Wiring

`Chacha20EvpManager::Get().Initialize()` is called in `LLP::Initialize()` (in
`src/LLP/global.cpp`) immediately before both mining servers are started:

```cpp
/* Initialize transport encryption mode for both mining lanes */
Chacha20EvpManager::Get().Initialize();

/* STATELESS_MINER_SERVER (port 9323) */
STATELESS_MINER_SERVER = new Server<StatelessMinerConnection>(CONFIG);

/* MINING_SERVER legacy lane (port 8323) */
MINING_SERVER = new Server<Miner>(LEGACY_CONFIG);
```

`Initialize()` is **idempotent**: calling it more than once is a no-op, guarded by an
`std::atomic<bool>` compare-exchange.

---

## Session Key / ChaCha20 Recovery Path

The EVP Manager does **not** touch session key derivation or the recovery cache.
`LLC::MiningSessionKeys::DeriveChaCha20Key()` and the `chacha20_context_persisted_in_recovery_cache`
path remain unchanged. The manager only gates the encrypt/decrypt at the transport layer.

---

## Future: TLS Plug-in

When TLS is implemented, the following stubs in `Chacha20EvpManager::Encrypt()` and
`Chacha20EvpManager::Decrypt()` will be replaced:

```cpp
if(eMode == MiningTransportMode::TLS)
{
    /* TLS encryption is not yet implemented — future stub */
    debug::error(FUNCTION, "TLS encrypt called but TLS is not yet implemented");
    return false;
}
```

TLS will be wired as a new `MiningTransportMode::TLS` branch that delegates to the
OpenSSL TLS layer instead of the AEAD payload helpers. No changes to `EVP` or `NONE`
branches will be required.

**Constraint preserved:** EVP and TLS will still be mutually exclusive — the `Initialize()`
conflict resolution logic and `IsEvpActive()`/`IsTlsActive()` invariant remain.

---

## Files

| Path | Role |
|------|------|
| `src/LLP/include/chacha20_evp_manager.h` | Header — singleton class + `MiningTransportMode` enum |
| `src/LLP/chacha20_evp_manager.cpp` | Implementation |
| `tests/unit/LLP/test_chacha20_evp_manager.cpp` | Catch2 unit tests |
| `docs/architecture/CHACHA20_EVP_MANAGER.md` | This document |

---

## Related

- `src/LLC/include/chacha20_helpers.h` — `EncryptPayloadChaCha20` / `DecryptPayloadChaCha20`
- `src/LLC/include/mining_session_keys.h` — `DeriveChaCha20Key`
- `src/LLP/stateless_miner.cpp` — `DecryptRewardPayload` / `EncryptRewardResult`
- `src/LLP/stateless_miner_connection.cpp` — SUBMIT_BLOCK decrypt call sites
- `docs/architecture/SIMLINK_DUAL_LANE_ARCHITECTURE.md` — dual-lane overview

---

## Follow-Up Work

The items below track wiring work completed in the PR that followed PR #417.

### ✅ Completed (this PR)

| Item | Location | Status |
|------|----------|--------|
| SESSION_STATUS incoming decrypt | `stateless_miner_connection.cpp` | ✅ Done — graceful fallback to plaintext |
| SESSION_STATUS_ACK outgoing encrypt | `stateless_miner_connection.cpp` | ✅ Done — MITM hardening for SessionID packets |
| SESSION_KEEPALIVE response encrypt | `stateless_miner.cpp::ProcessSessionKeepalive` | ✅ Done |
| KEEPALIVE_V2_ACK response encrypt | `stateless_miner.cpp::ProcessKeepaliveV2` | ✅ Done |
| `DecryptRewardPayload` EVP gate | `stateless_miner.cpp` | ✅ Done — `IsEvpActive()` guard added |
| `EncryptRewardResult` EVP gate | `stateless_miner.cpp` | ✅ Done — `IsEvpActive()` guard added |
| `prune_expired_sessions()` method | `chacha20_evp_manager.h` / `.cpp` | ✅ Done — stable hook; no-op until session key store added |
| `prune_expired_sessions` tie-in | `stateless_manager.cpp::CleanupSessionScopedMaps` | ✅ Done |

### 🔲 Remaining / Future

| Item | Notes |
|------|-------|
| SUBMIT_BLOCK decrypt | Currently uses `LLC::DecryptPayloadChaCha20` directly — acceptable (constraint 7) |
| GET_BLOCK / BLOCK_DATA body encryption | Template data; high-volume — low MITM risk; future work |
| MINER_AUTH_INIT / MINER_AUTH_CHALLENGE / MINER_AUTH_RESPONSE | Protected by Falcon key exchange; no plaintext session ID exposed |
| Internal session key store in `Chacha20EvpManager` | Required before `prune_expired_sessions` has real work to do |
| TLS mode implementation | Stub exists; awaiting TLS infrastructure |
