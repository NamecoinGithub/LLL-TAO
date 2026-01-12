# Archived Documentation

This directory contains deprecated documentation that has been superseded by newer implementations but is kept for historical reference.

## Contents

### GET_ROUND_PROTOCOL.md

**Status:** Deprecated  
**Superseded By:** ../STATELESS_MINING_PROTOCOL.md  
**Date Archived:** 2026-01-12

This document describes the legacy polling-based GET_ROUND protocol (opcodes 0x85, 0xCC) that was used before the push notification model was implemented. The protocol has been completely replaced by:

- **Push Notifications:** NEW_BLOCK (0xD009) replaces polling
- **Template Delivery:** GET_BLOCK (0xD008) delivers 216-byte templates
- **New Opcode Range:** StatelessMining protocol (0xD000-0xD00F)

**Why This Is Archived:**
- Describes polling model that creates unnecessary network traffic
- Uses old uint8_t opcodes (0x85, 0xCC) instead of new uint16_t opcodes (0xD000+)
- Documents 12-byte height responses instead of 216-byte templates
- Does not reflect the push notification architecture implemented in PR #170

**Historical Context:**
The GET_ROUND protocol was used to solve the FALSE OLD_ROUND rejection problem by providing channel-specific heights. While it fixed that issue, it relied on continuous polling which was inefficient. The current implementation uses server-push notifications to eliminate polling entirely while maintaining all the fixes from the GET_ROUND protocol.

**For Miner Developers:**
If you are developing or maintaining a miner, **do not implement the GET_ROUND protocol**. Use the current stateless mining protocol documented in:
- `../STATELESS_MINING_PROTOCOL.md` - Complete protocol specification
- `../TEMPLATE_FORMAT.md` - Wire format details
- `../MIGRATION_GET_ROUND_TO_GETBLOCK.md` - Migration guide

**For Historical Research:**
This document is preserved to understand the evolution of the Nexus mining protocol and the problems that led to the current push notification design.
