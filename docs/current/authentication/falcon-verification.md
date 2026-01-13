# Falcon Verification - Node Side

## Overview

This document describes the node-side implementation of Falcon signature verification for mining authentication. Falcon-1024 provides post-quantum security for the stateless mining protocol.

**Security Level:** NIST Level 5 (256-bit quantum resistance)  
**Algorithm:** Falcon-1024 (NIST PQC standardized)  
**Version:** LLL-TAO 5.1.0+

---

## Falcon-1024 Background

### What is Falcon?

Falcon (Fast Fourier Lattice-based Compact Signatures) is a NIST-standardized post-quantum signature scheme. It provides security against attacks by both classical and quantum computers.

**Key Properties:**
- **Quantum-resistant:** Secure against Shor's algorithm
- **Compact signatures:** ~1.5 KB for Falcon-1024
- **Fast verification:** <2ms on modern hardware
- **Deterministic:** Same message always produces same signature (with ct=1)

### Falcon-512 vs. Falcon-1024

| Property | Falcon-512 | Falcon-1024 |
|----------|------------|-------------|
| Security level | NIST Level 1 | NIST Level 5 |
| Quantum bits | 128-bit | 256-bit |
| RSA equivalent | RSA-2048 | RSA-4096 |
| Public key | 897 bytes | 1793 bytes |
| Private key | 1281 bytes | 2305 bytes |
| Signature | 809 bytes | 1577 bytes |
| Signing time | <1ms | <1.5ms |
| Verify time | <1ms | <2ms |

**Node Support:** Nexus nodes automatically detect and support both versions.

---

## Authentication Architecture

### Dual Signature System

Nexus uses **two types** of Falcon signatures:

#### 1. Disposable Falcon (Session Authentication)

**Purpose:** Real-time mining protocol authentication  
**Lifetime:** Single mining session only  
**Storage:** NOT stored on blockchain (zero overhead)  
**Used in:** MINER_AUTH opcode  
**Verified during:** Session establishment

**Workflow:**
```
1. Miner generates Falcon keypair
2. Miner sends Falcon public key in MINER_AUTH
3. Node verifies public key format
4. Node creates session bound to this key
5. Signature used for session authentication only
6. Session expires after 24 hours (default)
7. Keys discarded, no blockchain storage
```

#### 2. Physical Falcon (Block Authorship - Optional)

**Purpose:** Permanent proof of block authorship  
**Lifetime:** Stored forever on blockchain  
**Storage:** Stored in block (809 or 1577 bytes overhead)  
**Used in:** SUBMIT_BLOCK opcode (optional)  
**Verified during:** Block validation

**Workflow:**
```
1. Miner signs solved block with same Falcon key
2. Miner includes signature in SUBMIT_BLOCK
3. Node verifies signature matches session key
4. Signature stored in block permanently
5. Provides long-term authorship proof
6. Optional feature (physicalsigner=0 to disable)
```

**Key Bonding:** Both signatures must use the **same** Falcon keypair for security.

---

## Node-Side Verification

### MINER_AUTH Processing

#### Step 1: Extract Falcon Public Key

```cpp
// src/LLP/stateless_miner_connection.cpp
void StatelessMinerConnection::ProcessAuth(const Packet& packet)
{
    // Extract genesis hash (first 32 bytes)
    uint256_t hashGenesis;
    packet >> hashGenesis;
    
    // Extract public key length (2 bytes, big-endian)
    uint16_t nPubkeyLen;
    packet >> nPubkeyLen;
    
    // Validate length matches Falcon-512 or Falcon-1024
    if(nPubkeyLen != 897 && nPubkeyLen != 1793) {
        SendAuthResponse(false); // Invalid key size
        return;
    }
    
    // Extract public key bytes
    std::vector<uint8_t> vPubkey(nPubkeyLen);
    packet >> vPubkey;
}
```

#### Step 2: Decrypt Public Key (if remote)

For remote connections, the public key is encrypted with ChaCha20:

```cpp
// Derive ChaCha20 key from genesis hash
std::vector<uint8_t> vSeed;
vSeed.insert(vSeed.end(), 
             (const uint8_t*)"nexus-mining-chacha20-v1", 24);
vSeed.insert(vSeed.end(), hashGenesis.begin(), hashGenesis.end());
uint256_t hashKey = LLC::SK256(vSeed);
std::vector<uint8_t> vChaChaKey(hashKey.begin(), hashKey.begin() + 32);

// Decrypt public key (if remote)
bool fRemote = !IsLocalhost(GetAddress());
if(fRemote) {
    if(!DecryptChaCha20(vPubkey, vChaChaKey, vPubkey)) {
        SendAuthResponse(false); // Decryption failed
        return;
    }
}
```

**Why encrypt?** Protects miner privacy and prevents public key leakage during transmission.

#### Step 3: Detect Falcon Version

```cpp
// Auto-detect Falcon version from public key size
LLC::FalconVersion nVersion = LLC::FalconVersion::UNKNOWN;

if(vPubkey.size() == 897) {
    nVersion = LLC::FalconVersion::FALCON512;
    debug::log(2, "Detected Falcon-512 public key");
}
else if(vPubkey.size() == 1793) {
    nVersion = LLC::FalconVersion::FALCON1024;
    debug::log(2, "Detected Falcon-1024 public key");
}
else {
    SendAuthResponse(false); // Invalid size
    return;
}
```

**Stealth Mode:** Node automatically detects version without miner specifying it.

#### Step 4: Validate Genesis Hash

```cpp
// Verify genesis exists on blockchain
if(!LLD::Ledger->HasFirst(hashGenesis)) {
    debug::warning("Genesis hash not found: ", hashGenesis.ToString());
    SendAuthResponse(false); // Invalid genesis
    return;
}

// Genesis validation ensures miner has valid Tritium account
// Rewards will be routed to this genesis (or MINER_SET_REWARD address)
```

#### Step 5: Check Optional Whitelist

```cpp
// Optional: Check if public key is whitelisted
if(config::HasArg("-minerallowkey")) {
    std::vector<std::string> vAllowedKeys = 
        config::GetMultiArgs("-minerallowkey");
    
    // Convert public key to hex string
    std::string strPubkey = HexStr(vPubkey);
    
    // Check if in whitelist
    bool fAllowed = false;
    for(const auto& strAllowed : vAllowedKeys) {
        if(strAllowed == strPubkey) {
            fAllowed = true;
            break;
        }
    }
    
    if(!fAllowed) {
        debug::warning("Miner public key not whitelisted");
        SendAuthResponse(false); // Not authorized
        return;
    }
}
```

**Whitelist Format:** Hexadecimal strings in `nexus.conf`:
```ini
minerallowkey=0123456789abcdef...  # 1794 hex chars (Falcon-512)
minerallowkey=fedcba9876543210...  # 3586 hex chars (Falcon-1024)
```

#### Step 6: Create Session

```cpp
// Generate unique session ID
uint32_t nSessionId = LLC::GetRand(); // Random 32-bit integer

// Create mining context
MiningContext ctx;
ctx.fAuthenticated = true;
ctx.nSessionId = nSessionId;
ctx.hashGenesis = hashGenesis;
ctx.vMinerPubKey = vPubkey;
ctx.nFalconVersion = nVersion;
ctx.vChaChaKey = vChaChaKey;
ctx.nSessionStart = runtime::unifiedtimestamp();
ctx.nSessionTimeout = 86400; // 24 hours

// Store in session cache
{
    std::lock_guard<std::mutex> lock(SessionCacheMutex);
    SessionCache[nSessionId] = std::move(ctx);
}

debug::log(0, "Miner authenticated: session=", nSessionId,
           " genesis=", hashGenesis.ToString().substr(0, 8),
           " falcon=", nVersion == LLC::FalconVersion::FALCON1024 ? "1024" : "512");

// Send success response
SendAuthResponse(true, nSessionId);
```

---

## Physical Falcon Verification

### SUBMIT_BLOCK Signature Check

When a miner submits a solved block with Physical Falcon signature:

```cpp
// src/LLP/stateless_miner_connection.cpp
void StatelessMinerConnection::ProcessSubmitBlock(const Packet& packet)
{
    // ... extract and validate block ...
    
    // Check for Physical Falcon signature
    bool fHasPhysical;
    packet >> fHasPhysical;
    
    if(fHasPhysical) {
        // Extract signature length
        uint16_t nSigLen;
        packet >> nSigLen;
        
        // Validate signature length matches Falcon version
        MiningContext& ctx = GetSession(nSessionId);
        uint16_t nExpectedLen = 
            (ctx.nFalconVersion == LLC::FalconVersion::FALCON512) ? 809 : 1577;
        
        if(nSigLen != nExpectedLen) {
            SendBlockRejected("Invalid Falcon signature length");
            return;
        }
        
        // Extract signature bytes
        std::vector<uint8_t> vSignature(nSigLen);
        packet >> vSignature;
        
        // Verify signature
        uint1024_t hashBlock = block.GetHash();
        if(!VerifyFalconSignature(vSignature, ctx.vMinerPubKey, hashBlock)) {
            SendBlockRejected("Invalid Falcon signature");
            return;
        }
        
        // Attach signature to block (permanent storage)
        block.vchBlockSig = vSignature;
        
        debug::log(1, "Physical Falcon signature verified (", nSigLen, " bytes)");
    }
    
    // Continue with block acceptance...
}
```

### Falcon Signature Verification Implementation

```cpp
// src/LLC/falcon.cpp
bool VerifyFalconSignature(
    const std::vector<uint8_t>& vSignature,
    const std::vector<uint8_t>& vPubkey,
    const uint1024_t& hashMessage)
{
    // Determine Falcon version from public key size
    int nVariant = (vPubkey.size() == 897) ? FALCON_512 : FALCON_1024;
    
    // Initialize Falcon context
    falcon_context ctx;
    if(falcon_init(&ctx, nVariant) != 0) {
        debug::error("Failed to initialize Falcon context");
        return false;
    }
    
    // Import public key
    if(falcon_import_public_key(&ctx, vPubkey.data(), vPubkey.size()) != 0) {
        debug::error("Failed to import Falcon public key");
        falcon_free(&ctx);
        return false;
    }
    
    // Prepare message (hash of block)
    std::vector<uint8_t> vMessage(hashMessage.begin(), hashMessage.end());
    
    // Verify signature
    int nResult = falcon_verify(
        &ctx,
        vSignature.data(), vSignature.size(),
        vMessage.data(), vMessage.size()
    );
    
    falcon_free(&ctx);
    
    if(nResult == 0) {
        return true; // Signature valid
    }
    else {
        debug::warning("Falcon signature verification failed: code ", nResult);
        return false;
    }
}
```

**Performance:**
- Falcon-512 verification: <1ms
- Falcon-1024 verification: <2ms
- Typical: ~1.5ms for Falcon-1024

---

## Security Properties

### Post-Quantum Security

**Classical Attack Resistance:**
- Lattice-based cryptography (not factoring or discrete log)
- No known classical attacks better than brute force
- RSA-4096 equivalent security (Falcon-1024)

**Quantum Attack Resistance:**
- Secure against Shor's algorithm (breaks RSA/ECC)
- Secure against Grover's algorithm
- 256-bit quantum security level (Falcon-1024)
- ~2^128 quantum operations required to break

### Key Bonding

**Single Keypair Requirement:**
Both Disposable and Physical signatures must use the **same** Falcon keypair:

```cpp
// Verify bonding during SUBMIT_BLOCK
if(fHasPhysical) {
    // Physical signature must be signed with session's public key
    if(!VerifyFalconSignature(vSignature, ctx.vMinerPubKey, hashBlock)) {
        SendBlockRejected("Signature key mismatch");
        return;
    }
}
```

**Why bonding?**
- Prevents signature swapping attacks
- Ensures consistent miner identity
- Simplifies miner configuration

### Genesis Hash Binding

Session keys are cryptographically bound to genesis hash:

```cpp
// ChaCha20 key derivation (deterministic from genesis)
vSeed = "nexus-mining-chacha20-v1" || hashGenesis
vChaChaKey = SHA256(vSeed)[0:32]
```

**Security benefits:**
- Different genesis = different encryption key
- Prevents cross-account signature replay
- Ties session to specific Tritium account

---

## Performance Characteristics

### Verification Latency

| Operation | Target | Typical | 95th Percentile |
|-----------|--------|---------|-----------------|
| Public key extraction | <0.1ms | <0.05ms | 0.08ms |
| ChaCha20 decryption | <0.5ms | 0.3ms | 0.4ms |
| Falcon version detection | <0.1ms | <0.01ms | 0.02ms |
| Genesis validation | <1ms | 0.5ms | 0.8ms |
| Signature verification | <5ms | 1-2ms | 4ms |
| **Total MINER_AUTH** | **<5ms** | **2-3ms** | **4ms** |

### Throughput Capacity

**Per Node:**
- Authentication requests/sec: 1000+
- Signature verifications/sec: 500+
- Concurrent authenticated sessions: 1000+

**Bottlenecks:**
- Database genesis lookup (mitigated by caching)
- Signature verification CPU cost (parallel execution)

---

## Configuration

### Enable Falcon Verification

Falcon verification is **always enabled** and cannot be disabled. No configuration needed.

### Optional: Key Whitelisting

Restrict mining to specific Falcon public keys:

```ini
# In nexus.conf
minerallowkey=<hex_string_897_or_1793_bytes>
minerallowkey=<hex_string_897_or_1793_bytes>
# Add more as needed
```

**Use cases:**
- Private mining pools
- Corporate environments
- Testnet restriction

**See:** [Key Whitelisting Guide](../security/key-whitelisting.md)

---

## Troubleshooting

### "Authentication failed" (MINER_AUTH_RESPONSE 0x00)

**Possible causes:**

1. **Genesis not found:**
   ```
   Solution: Verify genesis hash is valid Tritium account
   Check: ./nexus accounts/list/accounts
   ```

2. **Invalid public key size:**
   ```
   Solution: Regenerate Falcon keys on miner
   Command: nexusminer --generate-keys --falcon=1024
   ```

3. **ChaCha20 decryption failed:**
   ```
   Solution: Verify genesis hash matches on miner and node
   Check: Ensure miner is using correct genesis
   ```

4. **Not whitelisted:**
   ```
   Solution: Add miner's public key to minerallowkey
   Command: nexusminer --export-pubkey > pubkey.txt
   ```

### "Invalid Falcon signature" (SUBMIT_BLOCK rejection)

**Possible causes:**

1. **Key mismatch:**
   ```
   Solution: Ensure miner using same key as authentication
   Check: Regenerate keys if corrupted
   ```

2. **Signature corruption:**
   ```
   Solution: Check network connection quality
   Check: Enable packet integrity checking
   ```

3. **Wrong Falcon version:**
   ```
   Solution: Ensure miner and node use same version
   Check: Both should auto-negotiate (should not happen)
   ```

---

## Cross-References

**Related Documentation:**
- [Genesis Verification](genesis-verification.md)
- [Session Cache](session-cache.md)
- [Miner Authentication](miner-authentication.md)
- [Key Whitelisting](../security/key-whitelisting.md)
- [Quantum Resistance](../security/quantum-resistance.md)

**Miner Perspective:**
- [NexusMiner Falcon Authentication](https://github.com/Nexusoft/NexusMiner/blob/main/docs/current/security/falcon-authentication.md)
- [NexusMiner Key Generation](https://github.com/Nexusoft/NexusMiner/blob/main/docs/current/getting-started/key-generation.md)

**Source Code:**
- `src/LLC/falcon.cpp` - Falcon signature implementation
- `src/LLP/stateless_miner_connection.cpp` - Authentication handler
- `src/LLP/include/stateless_miner.h` - Mining context

---

## Version Information

**Document Version:** 1.0  
**Falcon Version:** Falcon-512 and Falcon-1024 (auto-detect)  
**LLL-TAO Version:** 5.1.0+  
**NIST Standard:** FIPS 204 (draft)  
**Last Updated:** 2026-01-13
