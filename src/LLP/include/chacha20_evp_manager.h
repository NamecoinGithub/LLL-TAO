/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLP_INCLUDE_CHACHA20_EVP_MANAGER_H
#define NEXUS_LLP_INCLUDE_CHACHA20_EVP_MANAGER_H

// Chacha20_EVP_Manager — NODE-side transport encryption gate
// Singleton. Owns the EVP vs TLS mode decision for the entire node.
// Both lanes (Legacy 8323 + Stateless 9323) query this manager.
//
// Rules:
//  - EVP (ChaCha20-Poly1305 via OpenSSL EVP) is DEFAULT.
//  - TLS is opt-in via -miningtls config arg.
//  - EVP and TLS are MUTUALLY EXCLUSIVE for the whole node / both lanes.
//  - If both are somehow set, EVP wins + warning logged.
//
// Thread-safe: all state is atomic or read-only after Initialize().

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace LLP
{

    /** MiningTransportMode
     *
     *  Transport encryption mode for the mining protocol.
     *  Applies node-wide to both the legacy (8323) and stateless (9323) lanes.
     *
     **/
    enum class MiningTransportMode : uint8_t
    {
        EVP  = 0,   // ChaCha20-Poly1305 via OpenSSL EVP (default)
        TLS  = 1,   // TLS (opt-in, future)
        NONE = 2    // No encryption (disabled for remote miners — allowed only localhost)
    };


    /** Chacha20EvpManager
     *
     *  NODE-side transport encryption gate. Singleton.
     *
     *  Acts as the authoritative transport-encryption gate for both the Legacy Lane
     *  (port 8323) and the Stateless Lane (port 9323). Owns the EVP vs TLS mode
     *  decision for the entire running node.
     *
     *  EVP and TLS are MUTUALLY EXCLUSIVE: if both are somehow configured, EVP wins
     *  and a warning is logged. Default mode is EVP (ChaCha20-Poly1305 via OpenSSL).
     *
     *  Thread-safe: all mutable state is atomic. After Initialize() returns, the mode
     *  is fixed for the lifetime of the node process.
     *
     *  Encrypt() / Decrypt() delegate directly to LLC::EncryptPayloadChaCha20 /
     *  LLC::DecryptPayloadChaCha20. They do not replace those helpers.
     *
     *  @see LLC::EncryptPayloadChaCha20
     *  @see LLC::DecryptPayloadChaCha20
     *  @see docs/architecture/CHACHA20_EVP_MANAGER.md
     *
     **/
    class Chacha20EvpManager
    {
    public:

        /** Get
         *
         *  Get the global singleton instance.
         *
         *  @return Reference to the singleton Chacha20EvpManager.
         *
         **/
        static Chacha20EvpManager& Get();


        /** Initialize
         *
         *  Called once at node startup (from wherever mining servers are initialized).
         *  Reads config flags, sets mode, logs the decision.
         *
         *  Config args consulted:
         *    -miningevp   (default true)  — enable ChaCha20-Poly1305 EVP mode
         *    -miningtls   (default false) — enable TLS mode (opt-in, future)
         *
         *  Conflict resolution: if both -miningevp and -miningtls are true, EVP wins
         *  and a warning is emitted.
         *
         *  Idempotent: calling Initialize() more than once is a no-op.
         *
         **/
        void Initialize();


        /** GetMode
         *
         *  Query the current transport encryption mode.
         *
         *  @return Current MiningTransportMode.
         *
         **/
        MiningTransportMode GetMode() const;


        /** IsEvpActive
         *
         *  Check if EVP (ChaCha20-Poly1305) mode is active.
         *
         *  @return true if mode == EVP.
         *
         **/
        bool IsEvpActive() const;


        /** IsTlsActive
         *
         *  Check if TLS mode is active.
         *
         *  @return true if mode == TLS.
         *
         **/
        bool IsTlsActive() const;


        /** IsEncryptionRequired
         *
         *  Returns true when remote miners MUST encrypt their packets.
         *  Currently true whenever mode == EVP (the default and only production mode).
         *  Remote miners connecting in NONE mode will be rejected (localhost only).
         *
         *  @return true if encryption is required (mode == EVP).
         *
         **/
        bool IsEncryptionRequired() const;


        /** Encrypt
         *
         *  Encrypt a plaintext payload using the active transport mode.
         *
         *  When mode == EVP: delegates to LLC::EncryptPayloadChaCha20.
         *  When mode == TLS: logs an error and returns false (future stub).
         *  When mode == NONE: not valid for remote peers.
         *
         *  @param[in]  vPlain  Plaintext payload to encrypt.
         *  @param[in]  vKey    32-byte symmetric session key.
         *  @param[out] vOut    Encrypted output (nonce + ciphertext + tag).
         *
         *  @return true on success, false on failure.
         *
         **/
        bool Encrypt(const std::vector<uint8_t>& vPlain,
                     const std::vector<uint8_t>& vKey,
                     std::vector<uint8_t>& vOut) const;


        /** Decrypt
         *
         *  Decrypt a ciphertext payload using the active transport mode.
         *
         *  When mode == EVP: delegates to LLC::DecryptPayloadChaCha20.
         *  When mode == TLS: logs an error and returns false (future stub).
         *
         *  @param[in]  vCipher Ciphertext payload (nonce + ciphertext + tag).
         *  @param[in]  vKey    32-byte symmetric session key.
         *  @param[out] vOut    Decrypted plaintext output.
         *
         *  @return true on success, false on failure.
         *
         **/
        bool Decrypt(const std::vector<uint8_t>& vCipher,
                     const std::vector<uint8_t>& vKey,
                     std::vector<uint8_t>& vOut) const;


        /** AllowPlaintext
         *
         *  Gate check: returns true only when plaintext (unencrypted) packets are
         *  acceptable from the peer at strPeerIP.
         *
         *  Plaintext is allowed ONLY for localhost miners (127.0.0.1) when
         *  IsEncryptionRequired() is false. All other configurations must encrypt.
         *
         *  Called at SUBMIT_BLOCK and any future encrypted-opcode handler.
         *
         *  @param[in] strPeerIP  Dotted-decimal IP of the connecting peer.
         *
         *  @return true if this peer may send unencrypted packets.
         *
         **/
        bool AllowPlaintext(const std::string& strPeerIP) const;


        /** ModeString
         *
         *  Returns a human-readable string describing the current mode.
         *  Suitable for startup log messages.
         *
         *  @return "EVP (ChaCha20-Poly1305)" | "TLS" | "NONE".
         *
         **/
        std::string ModeString() const;


    private:

        /** Default constructor — private; use Get(). **/
        Chacha20EvpManager() = default;

        /** Deleted copy/move to enforce singleton semantics. **/
        Chacha20EvpManager(const Chacha20EvpManager&)            = delete;
        Chacha20EvpManager& operator=(const Chacha20EvpManager&) = delete;
        Chacha20EvpManager(Chacha20EvpManager&&)                 = delete;
        Chacha20EvpManager& operator=(Chacha20EvpManager&&)      = delete;

        /** Active transport mode. EVP is the default. **/
        std::atomic<MiningTransportMode> m_mode{MiningTransportMode::EVP};

        /** Guards against repeated Initialize() calls. **/
        std::atomic<bool> m_initialized{false};
    };

} // namespace LLP

#endif // NEXUS_LLP_INCLUDE_CHACHA20_EVP_MANAGER_H
