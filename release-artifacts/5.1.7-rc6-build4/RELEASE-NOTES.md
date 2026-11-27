## Nexus Node 5.1.7-rc6 build 4

This release candidate includes the latest mining-readiness fixes and replaces
build 3. It contains Linux, macOS and Windows x86_64 command-line nodes.

### Changes since build 3

- Improve mining-readiness concurrency, peer-evidence handling and cache
  invalidation under peer changes and slow ledger reads.
- Show the loaded best-block height at startup and activated block heights
  during synchronization.
- Increment the node build number; the protocol version is unchanged.

### Important

- This is a release candidate. Controlled multi-node testing is still needed,
  especially for partitions, peer churn and extended recovery.
- Back up node and wallet data before upgrading. Windows legacy-wallet
  compatibility with Berkeley DB 5.3 has not been verified.
- The binaries are unsigned; macOS is not notarized. The Linux binary requires
  glibc 2.17 or newer. macOS targets Intel x86_64 and macOS 10.13 or newer.
- Public-network mining readiness requires initial synchronization and, by
  default, fresh supporting evidence from two distinct peer IP addresses.
  `system/get/info` exposes the last mining-readiness evaluation.
- Use a clean build when compiling from source; mixing old and new object
  layouts is unsafe.

Verify the accompanying `.sha256` files before extracting the archives; each
archive also includes `SHA256SUMS.txt` and source/build provenance.

### Verification

- Focused mining-readiness tests: 39 cases, 5,827 assertions.
- Linux, macOS and Windows binaries passed native startup and API version
  checks; the final archives were verified.
- A clean full-suite result is not yet available.

**Source:** `9ac000957b34151f77a3cc12be80ea2ad77bbd90`

**Changes:** https://github.com/NamecoinGithub/LLL-TAO/compare/nexus-node-5.1.7-rc6-build3...9ac000957b34151f77a3cc12be80ea2ad77bbd90
