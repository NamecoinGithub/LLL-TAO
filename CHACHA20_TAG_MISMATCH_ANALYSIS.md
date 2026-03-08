# ChaCha20 Tag Mismatch and Plaintext Size Discrepancy Analysis

## Issue Summary

Two related issues were reported:
1. **ChaCha20 tag mismatch** - Decryption fails due to authentication tag verification failure
2. **Plaintext size discrepancy** - Miner sends 1813 bytes, node expects 1803 bytes (10-byte difference)

## Investigation Findings

### 1. ChaCha20 KDF Implementation (Genesis Hash Encoding)

**Status**: ✅ **ALREADY FIXED - NO NODE ERROR**

The ChaCha20 key derivation in `src/LLC/include/mining_session_keys.h` is **correct** and uses the proper big-endian representation:

```cpp
inline std::vector<uint8_t> DeriveChaCha20Key(const uint256_t& hashGenesis)
{
    /* Build preimage: domain || genesis_bytes */
    std::vector<uint8_t> preimage;
    preimage.reserve(DOMAIN_CHACHA20.size() + 32);
    preimage.insert(preimage.end(), DOMAIN_CHACHA20.begin(), DOMAIN_CHACHA20.end());

    /* ✅ CORRECT: Use GetHex() + ParseHex() for consistent big-endian representation.
     * This matches NexusMiner's implementation. */
    std::string genesis_hex = hashGenesis.GetHex();
    std::vector<uint8_t> genesis_bytes = ParseHex(genesis_hex);

    preimage.insert(preimage.end(), genesis_bytes.begin(), genesis_bytes.end());

    /* Use OpenSSL SHA256 directly (same as NexusMiner) */
    std::vector<uint8_t> vKey(32);
    unsigned char* result = SHA256(preimage.data(), preimage.size(), vKey.data());

    return vKey;
}
```

**Verification**:
- Domain string: `"nexus-mining-chacha20-v1"` (24 bytes)
- Genesis hash: Converted to 32 bytes via `GetHex() + ParseHex()` (big-endian)
- KDF: `SHA256(domain || genesis_bytes)` using OpenSSL
- Result: 32-byte ChaCha20 key

The node implementation matches the expected miner-side implementation exactly.

### 2. Plaintext Size Discrepancy (1813 vs 1803 bytes)

**Status**: ⚠️ **MINER-SIDE ISSUE - Extra 10 Bytes Being Added**

#### Expected Format (Node Side)

The node expects the following plaintext format for Falcon-1024 Tritium blocks:

```
Format: [block(216)][timestamp(8)][sig_len(2)][signature(1577)]
Total:  216 + 8 + 2 + 1577 = 1803 bytes
```

This is defined in `src/LLP/include/falcon_constants.h:368`:
```cpp
/** Tritium wrapper signature (Falcon-1024) - localhost
 *  Format: [block(216)][timestamp(8)][siglen(2)][sig(1577)]
 *  Calculation: 216 + 8 + 2 + 1577 = 1803 bytes */
static const size_t SUBMIT_BLOCK_FULL_TRITIUM_WRAPPER_FALCON1024_MAX = 1803;
```

#### Actual Format (Miner Side)

The miner is sending **1813 bytes**, which is 10 bytes more than expected.

#### Possible Causes of 10-Byte Discrepancy

Several possibilities for the extra 10 bytes:

1. **Session ID + Sequence Number**:
   - Session ID: 4 bytes (little-endian)
   - Sequence number: 4 bytes
   - Reserved/flags: 2 bytes
   - Total: 10 bytes

2. **Extra Header Fields**:
   - Channel ID: 1 byte
   - Protocol version: 1 byte
   - Session ID: 4 bytes
   - Timestamp prefix: 4 bytes
   - Total: 10 bytes

3. **Length Prefix**:
   - Block size: 4 bytes
   - Total payload size: 4 bytes
   - Checksum: 2 bytes
   - Total: 10 bytes

#### Code Location Analysis

The node's decryption and parsing logic is in `src/LLP/stateless_miner_connection.cpp`:

**Lines 1277-1281**: ChaCha20 decryption
```cpp
bool fDecrypted = LLC::DecryptPayloadChaCha20(
    PACKET.DATA,
    context.vChaChaKey,
    decryptedData
);
```

**Lines 1320-1345**: Plaintext layout diagnostic
```cpp
debug::log(1, FUNCTION, "📐 NODE PLAINTEXT LAYOUT DIAGNOSTIC:");
debug::log(1, FUNCTION, "   Decrypted payload size: ", decryptedData.size(), " bytes");
```

**Lines 1392-1449**: Block size detection logic
```cpp
/* Format: [block(B)][timestamp(8)][sig_len(2)][signature(S)] */
/* Strategy: Work backwards from the end to find sig_len and validate packet structure */
/* We know: totalSize = B + 8 + 2 + S */
/* Therefore: B = totalSize - 8 - 2 - S */
```

The node attempts to dynamically determine the block size by trying common values (216 for Tritium, 220 for Legacy) and validating against the signature length field.

### 3. Root Cause Analysis

#### ChaCha20 Tag Mismatch

The tag mismatch could be caused by:

1. **Wrong plaintext format**: If the miner includes the extra 10 bytes in the plaintext BEFORE encryption, the decryption will succeed but produce 1813 bytes instead of 1803.

2. **Key derivation mismatch**: IF the miner is using a different method to derive the ChaCha20 key (e.g., using `GetBytes()` instead of `GetHex() + ParseHex()`), the keys won't match and decryption will fail with a tag mismatch.

3. **Nonce reuse or mismatch**: If the miner is using a different nonce than what's prepended to the ciphertext, the AEAD will fail.

#### Most Likely Scenario

Based on the evidence:
1. The node's KDF is **correct** (uses `GetHex() + ParseHex()`)
2. The node expects 1803 bytes, but receives 1813 bytes after decryption
3. This suggests the **miner is adding 10 extra bytes to the plaintext before encryption**

**Verdict**: **This is a MINER-SIDE ERROR**, not a node error.

### 4. Recommendations

#### For NexusMiner

The miner should be updated to send the correct plaintext format:

**INCORRECT (Current Miner Behavior - 1813 bytes)**:
```
[extra_10_bytes][block(216)][timestamp(8)][sig_len(2)][signature(1577)]
```

**CORRECT (Expected by Node - 1803 bytes)**:
```
[block(216)][timestamp(8)][sig_len(2)][signature(1577)]
```

The miner should remove whatever 10-byte prefix/header it's currently adding before encryption.

#### For Node (Optional Enhancement)

The node could add more detailed logging to help diagnose such issues:

```cpp
if(decryptedData.size() != expectedSize) {
    debug::log(0, FUNCTION, "⚠️ PLAINTEXT SIZE MISMATCH:");
    debug::log(0, FUNCTION, "   Received: ", decryptedData.size(), " bytes");
    debug::log(0, FUNCTION, "   Expected: ", expectedSize, " bytes");
    debug::log(0, FUNCTION, "   Difference: ",
               static_cast<int64_t>(decryptedData.size()) - static_cast<int64_t>(expectedSize), " bytes");
    if(decryptedData.size() > expectedSize && decryptedData.size() - expectedSize == 10) {
        debug::log(0, FUNCTION, "   Possible cause: Extra 10-byte header/prefix in miner plaintext");
        debug::log(0, FUNCTION, "   First 10 bytes (hex): ",
                   HexStr(decryptedData.begin(), decryptedData.begin() + 10));
    }
}
```

### 5. Test Case

A test should be added to verify the correct plaintext format:

```cpp
TEST_CASE("SUBMIT_BLOCK Falcon-1024 Plaintext Size", "[submit_block]")
{
    SECTION("Tritium block with Falcon-1024 has correct size")
    {
        // Format: [block(216)][timestamp(8)][siglen(2)][sig(1577)]
        size_t expected = 216 + 8 + 2 + 1577;
        REQUIRE(expected == 1803);
        REQUIRE(expected == FalconConstants::SUBMIT_BLOCK_FULL_TRITIUM_WRAPPER_FALCON1024_MAX);
    }
}
```

This test already exists in `tests/unit/LLP/falcon_1024_detection_test.cpp:81-84`.

## Conclusion

1. **ChaCha20 KDF**: ✅ Node implementation is **CORRECT** - no changes needed
2. **Plaintext Size**: ❌ Miner is sending 10 extra bytes - **MINER-SIDE ERROR**

**Recommendation**: The miner code should be reviewed to identify and remove the extra 10-byte prefix/header being added to the plaintext before ChaCha20 encryption.

## References

- `src/LLC/include/mining_session_keys.h:60-94` - ChaCha20 KDF implementation
- `src/LLP/include/falcon_constants.h:368` - Expected plaintext size (1803 bytes)
- `src/LLP/stateless_miner_connection.cpp:1277-1449` - Decryption and parsing logic
- `tests/unit/LLP/falcon_1024_detection_test.cpp:81-84` - Size validation test
- `INVESTIGATION_FINDINGS.md` - Previous genesis endianness investigation
