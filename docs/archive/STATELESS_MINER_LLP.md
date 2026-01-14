# Phase 2 Stateless Miner LLP Implementation

## Overview

This implementation adds a dedicated **Stateless Miner LLP server** on `miningport` that uses the pure functional `StatelessMiner` processor for Phase 2 mining protocol.

## Architecture

### Key Components

1. **StatelessMiner** (`src/LLP/stateless_miner.cpp`, `src/LLP/include/stateless_miner.h`)
   - Pure functional packet processor
   - Immutable `MiningContext` for state management
   - Returns `ProcessResult` with updated context and optional response packet
   - Handlers for Phase 2 protocol:
     - `ProcessFalconResponse`: Falcon authentication
     - `ProcessSetChannel`: Channel selection (Prime/Hash)
     - `ProcessGetBlock`: Block template requests
     - `ProcessSubmitBlock`: Block submission
     - `ProcessSessionKeepalive`: Session maintenance

2. **StatelessMinerConnection** (`src/LLP/stateless_miner_connection.cpp`, `src/LLP/types/stateless_miner_connection.h`)
   - Connection wrapper integrating `StatelessMiner` with LLP server framework
   - Manages connection lifecycle and context updates
   - Routes packets through `StatelessMiner::ProcessPacket`

3. **STATELESS_MINER_SERVER** (initialized in `src/LLP/global.cpp`)
   - LLP server instance on `miningport`
   - Starts when `-mining=1` is configured
   - Requires `miningpubkey` in `nexus.conf`

## Protocol Flow

### Phase 2 Mining Protocol

1. **Connection**
   ```
   Miner connects to node:miningport
   ```

2. **Falcon Authentication**
   ```
   Miner -> Node: FALCON_RESPONSE (pubkey + signature + optional genesis)
   Node  -> Miner: FALCON_VERIFY_OK (sessionId)
   ```

3. **Channel Selection**
   ```
   Miner -> Node: SET_CHANNEL (1=Prime, 2=Hash)
   Node  -> Miner: CHANNEL_ACK
   ```

4. **Block Mining Loop**
   ```
   Miner -> Node: GET_BLOCK
   Node  -> Miner: BLOCK_DATA (serialized block template)
   
   [Miner performs PoW]
   
   Miner -> Node: SUBMIT_BLOCK (merkle_root + nonce)
   Node  -> Miner: BLOCK_ACCEPTED | BLOCK_REJECTED
   ```

5. **Session Maintenance**
   ```
   Miner -> Node: SESSION_KEEPALIVE (periodic)
   ```

## Packet Definitions

All packet opcodes align with `miner.h` and NexusMiner Phase 2 protocol:

| Opcode | Name               | Direction    | Description                      |
|--------|--------------------|--------------|----------------------------------|
| 0      | BLOCK_DATA         | Node→Miner   | Block template for mining        |
| 1      | SUBMIT_BLOCK       | Miner→Node   | Submit solved block              |
| 3      | SET_CHANNEL        | Miner→Node   | Select mining channel            |
| 129    | GET_BLOCK          | Miner→Node   | Request block template           |
| 200    | BLOCK_ACCEPTED     | Node→Miner   | Block submission accepted        |
| 201    | BLOCK_REJECTED     | Node→Miner   | Block submission rejected        |
| 206    | CHANNEL_ACK        | Node→Miner   | Channel selection acknowledged   |
| 209    | FALCON_RESPONSE    | Miner→Node   | Falcon auth response             |
| 210    | FALCON_VERIFY_OK   | Node→Miner   | Falcon auth success              |
| 211    | SESSION_START      | Miner→Node   | Start session (future use)       |
| 212    | SESSION_KEEPALIVE  | Miner→Node   | Session keepalive                |

## Configuration

### nexus.conf

```conf
# Enable mining server
mining=1

# Mining public key (required)
miningpubkey=<falcon_pubkey_hex>

# Mining port (default: mainnet=9325, testnet=8325)
miningport=9325

# Mining threads (default: 4)
miningthreads=4

# Mining timeout (default: 30 seconds)
miningtimeout=30

# TLS/SSL Configuration (optional)
# For localhost debugging, TLS is typically not needed
# For production, use proper certificates

# Enable SSL/TLS for mining connections
miningssl=0  # Default: disabled (plaintext)

# Require SSL (reject plaintext connections)
miningsslrequired=0  # Default: SSL not required

# SSL certificate and key (both required if using SSL)
# If not specified, ephemeral self-signed cert will be auto-generated
# sslcertificate=/path/to/cert.pem
# sslcertificatekey=/path/to/key.pem
```

### TLS/SSL Configuration Notes

**For Localhost Debugging:**
- Set `miningssl=0` (default) for plaintext connections
- This avoids certificate complexity during development
- Miner and node must both use plaintext

**For Production:**
- Set `miningssl=1` to enable TLS
- Provide proper certificates via `sslcertificate` and `sslcertificatekey`
- Or allow auto-generated self-signed cert (less secure)
- Set `miningsslrequired=1` to reject plaintext connections

**Common Configuration Errors:**
- **TLS/Plaintext Mismatch**: Miner using TLS, node using plaintext (or vice versa)
  - Symptom: Large bogus packet lengths (e.g., 478113)
  - Solution: Ensure both miner and node use same protocol (both TLS or both plaintext)

- **Missing Certificates**: `miningssl=1` without valid certificate files
  - Node will auto-generate ephemeral cert with warning
  - Or fail to start if `miningsslrequired=1`

- **ChaCha20 Key Mismatch**: Using wrong genesis hash for encryption key
  - Symptom: Framing errors, invalid packet lengths
  - Solution: Ensure miner and node use same genesis block

## Protocol Isolation

The stateless miner server (`miningport`) operates in strict protocol isolation mode:

**Accepted Opcodes:**
- Phase 2 stateless opcodes: `0xD000-0xDFFF` range
- Backward-compatible legacy opcodes:
  - `GET_BLOCK (0x0081)`
  - `SUBMIT_BLOCK (0x0001)`
  - `GET_HEIGHT (0x0082)`
  - `GET_REWARD (0x0083)`
  - `GET_ROUND (0x0085)`
  - `SET_CHANNEL (0x0003)`
  - `MINER_AUTH_INIT (0x00CF)`
  - `MINER_AUTH_RESPONSE (0x00D1)`
  - `MINER_READY (0x00D8)`
  - `SESSION_KEEPALIVE (0x00D4)`

**Rejected Opcodes:**
- Pure legacy opcodes not in the supported set
- Attempting to use unsupported legacy opcodes will result in:
  - Error log with opcode, protocol, length, and remote IP:port
  - Immediate disconnection with explicit reason
  - Diagnostic message suggesting correct protocol

**Authentication Gate:**
- Before Falcon authentication completes, only auth-related opcodes are accepted
- Mining operations (GET_BLOCK, SUBMIT_BLOCK) require prior authentication
- Unauthorized packets receive AUTH_REQUIRED response before disconnect

**Defensive Packet Length Parsing:**
- All packets are validated against maximum length limits before processing
- Limits vary by packet type:
  - General packets: 4 KB
  - Auth packets: 8 KB (Falcon-1024 signatures)
  - Block packets: 24 KB
  - Absolute max: 32 KB for any packet
- Packets exceeding limits trigger:
  - Error log with hex dump of packet header
  - Diagnostic hints (TLS mismatch, legacy opcode, framing error)
  - Immediate disconnection

These protections prevent protocol confusion and help diagnose misconfigurations.

## Logging

The implementation includes comprehensive logging for debugging:

- **Connection events**: New connections, disconnections
- **Falcon auth**: Auth requests, verification results, session IDs
- **Channel selection**: Channel changes
- **Block operations**: GET_BLOCK requests, SUBMIT_BLOCK results
- **Errors**: Authentication failures, invalid packets, processing errors

Use `-verbose=1` or higher to see detailed packet processing logs.

## Status and Next Steps

### Completed

- ✅ Phase 2 packet opcode definitions
- ✅ `StatelessMiner` packet handlers (functional signatures)
- ✅ `StatelessMinerConnection` wrapper class
- ✅ LLP server initialization on `miningport`
- ✅ Falcon authentication flow
- ✅ Channel selection logic
- ✅ Comprehensive logging framework

### TODO (Future PRs)

1. **Block Creation Integration** (`ProcessGetBlock`)
   - Integrate with `TAO::Ledger::Create::Block`
   - Implement block template serialization
   - Add block map management for tracking in-flight blocks

2. **Block Submission Integration** (`ProcessSubmitBlock`)
   - Parse merkle root and nonce from packet data
   - Validate and sign blocks
   - Process blocks through `TAO::Ledger::Process`
   - Implement accept/reject logic

3. **Session Management**
   - Session ID tracking and expiration
   - Multi-miner session coordination
   - Session-based block ownership

4. **Testing**
   - Unit tests for all packet handlers
   - Integration tests with NexusMiner
   - Load testing with multiple miners

## Design Rationale

### Why Dedicated Stateless Miner Server?

The existing `Miner` class uses a hybrid stateful/stateless approach with mutable state. The dedicated `StatelessMinerConnection` using `StatelessMiner` provides:

1. **Clear separation**: Mining protocol separate from legacy code
2. **Immutability**: Safer concurrent access via immutable `MiningContext`
3. **Testability**: Pure functions easier to unit test
4. **Maintainability**: Protocol changes isolated to `StatelessMiner`
5. **Forward compatibility**: Phase 2 protocol ready for future enhancements

### miningport Repurposing

Repurposing `miningport` for Phase 2 is acceptable because:

- Hard fork is planned
- Legacy miners will be upgraded to NexusMiner
- `miningport` rarely used in current deployments
- Clean break for new protocol

## Coordination with NexusMiner

This LLL-TAO PR must be deployed alongside the companion NexusMiner PR that:

- Connects to `miningport` as LLP endpoint
- Implements Falcon handshake client side
- Uses `SET_CHANNEL` / `GET_BLOCK` / `SUBMIT_BLOCK` instead of legacy `GET_HEIGHT`
- Supports both CPU and GPU mining with new architecture

> "To Man what is Impossible is ONLY possible with GOD"
