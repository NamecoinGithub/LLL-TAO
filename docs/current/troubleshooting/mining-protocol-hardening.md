# Mining Protocol Hardening - Testing and Validation Guide

## Overview

This document describes the mining protocol hardening changes implemented to prevent protocol confusion and diagnose connection issues between miners and the Nexus node.

## Changes Made

### 1. Maximum Packet Length Limits

**File:** `src/LLP/include/mining_limits.h` (new)

Added constants defining maximum packet sizes for mining LLP connections:
- `MAX_AUTH_PACKET_LENGTH`: 8 KB (Falcon-1024 auth packets)
- `MAX_BLOCK_PACKET_LENGTH`: 24 KB (block templates and submissions)
- `MAX_GENERAL_PACKET_LENGTH`: 4 KB (general mining packets)
- `MAX_ANY_PACKET_LENGTH`: 32 KB (absolute maximum)

**Rationale:** Prevents bogus LENGTH values like 478113 (reported issue) from reaching packet parser.

### 2. Defensive Length Parsing

**Files Modified:**
- `src/LLP/stateless_miner_connection.cpp`
- `src/LLP/miner.cpp`

**Changes:**
- Added length validation in `Event(EVENTS::HEADER)` for both stateless and legacy miners
- Packets exceeding limits are logged with diagnostic information:
  - Packet header (opcode) and protocol name
  - Packet length vs. maximum allowed
  - Hex dump of packet header bytes
  - Diagnostic hints (TLS mismatch, legacy opcode, framing error)
- Connection is disconnected with explicit reason on invalid length

### 3. Strict Protocol Isolation

**File:** `src/LLP/stateless_miner_connection.cpp`

**Changes:**
- Stateless miner server now enforces strict opcode filtering
- Accepts only:
  - Phase 2 stateless opcodes (0xD000-0xDFFF range)
  - Backward-compatible legacy opcodes (GET_BLOCK, SUBMIT_BLOCK, auth packets, etc.)
- Rejects unsupported legacy opcodes with:
  - Error log showing opcode, protocol, length, remote IP:port
  - Guidance message about supported opcodes
  - Immediate disconnection

### 4. Authentication Gate

**File:** `src/LLP/stateless_miner_connection.cpp`

**Changes:**
- Before Falcon authentication completes, only auth-related opcodes are accepted
- Non-auth packets receive AUTH_REQUIRED response before disconnect
- Prevents unauthorized mining operations

### 5. TLS/SSL Configuration Diagnostics

**File:** `src/LLP/global.cpp`

**Changes:**
- At startup, validates SSL configuration if `-miningssl=1`
- Checks for certificate and key files:
  - If both missing: Warns about ephemeral self-signed cert
  - If one missing: Error and disable SSL (or fail if SSL required)
  - If files don't exist: Error and disable SSL (or fail if SSL required)
- Logs SSL status at server start (enabled/disabled)
- Fail-fast mode: If `-miningsslrequired=1` but SSL config invalid, server won't start

### 6. Documentation Updates

**File:** `docs/archive/STATELESS_MINER_LLP.md`

**Added:**
- TLS/SSL configuration section with examples
- Common configuration error descriptions
- Protocol isolation explanation
- Defensive packet length parsing explanation
- Troubleshooting guidance for TLS/plaintext mismatches

## Testing Procedures

### Test 1: Normal Operation (No Changes Expected)

**Setup:**
```bash
# nexus.conf
mining=1
miningpubkey=<valid_falcon_key>
miningport=8323
```

**Test:**
1. Start node with mining enabled
2. Connect miner to port 8323
3. Verify normal authentication and mining
4. Check logs show no errors

**Expected:** No behavior change for valid packets

### Test 2: Invalid Packet Length Detection

**Simulated Issue:**
- Send packet with LENGTH > 32768 bytes

**Expected Logs:**
```
ERROR: INVALID PACKET LENGTH from 127.0.0.1:12345
  Header:   0x0081 (GET_BLOCK)
  Protocol: Legacy
  Length:   478113 bytes (max: 32768)
  Raw header bytes: 0081 00074b01
  This may indicate:
    - TLS/plaintext protocol mismatch
    - Legacy opcode on stateless port
    - Corrupted framing or ChaCha20 key mismatch
Disconnecting 127.0.0.1: Invalid packet length exceeds maximum
```

### Test 3: Legacy Opcode Rejection

**Simulated Issue:**
- Send unsupported legacy opcode (e.g., BLOCK_DATA=0x0000) to stateless port

**Expected Logs:**
```
ERROR: LEGACY OPCODE REJECTED on stateless port from 127.0.0.1:12345
  Header:   0x0000 (BLOCK_DATA)
  Protocol: Legacy
  Length:   216 bytes
  This opcode is not supported on the stateless miner port.
  Stateless port accepts:
    - Phase 2 opcodes: 0xD000-0xDFFF
    - Backward-compatible legacy opcodes: GET_BLOCK, SUBMIT_BLOCK, etc.
  Please ensure miner is using correct protocol version.
Disconnecting 127.0.0.1: Unsupported legacy opcode on stateless port
```

### Test 4: Authentication Gate

**Simulated Issue:**
- Send GET_BLOCK before completing Falcon authentication

**Expected Logs:**
```
WARNING: Non-auth opcode received before authentication from 127.0.0.1:12345
  Header:   0x0081 (GET_BLOCK)
  Authentication required before mining operations.
Disconnecting 127.0.0.1: Authentication required
```

### Test 5: SSL Configuration - Missing Certificates

**Setup:**
```bash
# nexus.conf
mining=1
miningssl=1  # Enable SSL without certificates
```

**Expected Logs:**
```
WARNING: === MINING SSL CONFIGURATION WARNING ===
Mining SSL enabled (-miningssl=1) without external certificates.
Server will use ephemeral self-signed certificate (auto-generated).

For localhost testing: This is acceptable.
For production: Configure proper certificates:
  -sslcertificate=/path/to/cert.pem
  -sslcertificatekey=/path/to/key.pem
========================================
Phase 2 Stateless Miner LLP server started on port 8323
  SSL/TLS: ENABLED
```

### Test 6: SSL Configuration - Invalid Certificate Path

**Setup:**
```bash
# nexus.conf
mining=1
miningssl=1
miningsslrequired=1
sslcertificate=/nonexistent/cert.pem
sslcertificatekey=/nonexistent/key.pem
```

**Expected Logs:**
```
ERROR: === MINING SSL CONFIGURATION ERROR ===
SSL certificate file not found or not readable:
  Path: /nonexistent/cert.pem

SSL is REQUIRED (-miningsslrequired=1)
Mining server will NOT start due to missing certificate.
======================================
```

**Expected:** Node fails to start (returns false from Initialize())

### Test 7: TLS/Plaintext Mismatch Detection

**Setup:**
- Node configured with `-miningssl=0` (plaintext)
- Miner connects with TLS enabled

**Expected:**
- Large bogus packet LENGTH values (TLS handshake bytes interpreted as LLP header)
- Caught by defensive length parsing
- Logs suggest TLS mismatch as possible cause

## Verification Checklist

After implementing these changes, verify:

- [ ] Node starts successfully with valid mining configuration
- [ ] Node logs SSL status at startup (ENABLED or DISABLED)
- [ ] Invalid packet lengths are caught and logged with diagnostics
- [ ] Unsupported legacy opcodes are rejected with clear error messages
- [ ] Authentication is enforced before mining operations
- [ ] SSL configuration errors are detected at startup
- [ ] Missing certificate files prevent SSL or cause fail-fast
- [ ] Miner connections work normally with valid configuration
- [ ] Diagnostic logs help identify TLS/plaintext mismatches

## Troubleshooting

### Issue: "INVALID PACKET LENGTH" with large values

**Possible Causes:**
1. **TLS/Plaintext Mismatch**
   - Node expects plaintext, miner sends TLS (or vice versa)
   - Solution: Ensure both use same protocol
   
2. **ChaCha20 Key Mismatch**
   - Wrong genesis hash used for encryption key derivation
   - Solution: Verify genesis hash matches between miner and node

3. **Network Corruption**
   - Proxy or firewall modifying packets
   - Solution: Test direct connection without intermediaries

### Issue: "LEGACY OPCODE REJECTED"

**Possible Causes:**
1. **Wrong Miner Version**
   - Miner using old protocol on new port
   - Solution: Update miner to Phase 2 protocol

2. **Wrong Port**
   - Connecting to stateless port with legacy protocol
   - Solution: Check port configuration

### Issue: "Authentication required"

**Possible Causes:**
1. **Missing Authentication**
   - Miner attempting mining operations before auth
   - Solution: Complete Falcon authentication first

2. **Auth Failure**
   - Previous authentication attempt failed
   - Solution: Check auth logs, verify Falcon keys

### Issue: SSL Configuration Errors

**Possible Causes:**
1. **Missing Certificates**
   - `-miningssl=1` without certificate files
   - Solution: Provide certificates or accept ephemeral cert

2. **Invalid Certificate Paths**
   - Files don't exist or aren't readable
   - Solution: Fix paths or disable SSL

3. **SSL Required Without SSL Enabled**
   - `-miningsslrequired=1` with `-miningssl=0`
   - Solution: Enable SSL or remove SSL requirement

## Performance Impact

These changes have **minimal performance impact**:
- Length validation: One integer comparison per packet header (negligible)
- Opcode filtering: One range check per packet (negligible)
- SSL validation: Only at startup (no runtime impact)
- Logging: Only on errors (not on success path)

## Security Benefits

1. **Prevents Protocol Confusion**
   - Clear separation between legacy and stateless protocols
   - Authentication enforcement prevents unauthorized mining

2. **Defends Against Malformed Packets**
   - Rejects absurdly large LENGTH values early
   - Prevents buffer overflows or memory issues

3. **Improves Diagnostics**
   - Clear error messages help identify issues quickly
   - Reduces debugging time for configuration problems

4. **SSL Configuration Safety**
   - Prevents silent failures with invalid SSL config
   - Makes TLS/plaintext state explicit in logs

## Backward Compatibility

These changes maintain full backward compatibility:
- Valid packets are processed identically
- Supported legacy opcodes still work
- Only invalid/malicious packets are rejected
- No protocol version changes required

## Future Enhancements

Potential future improvements:
1. Add connection state logging (SSL vs. plaintext) when accepting connection
2. Implement packet header signature/checksum for additional validation
3. Add metrics for rejected packets (DDOS detection)
4. Create automated test suite for protocol isolation
5. Add support for connection migration (TLS upgrade after plaintext start)
