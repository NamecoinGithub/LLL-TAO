/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once
#ifndef NEXUS_LLC_CHACHA20_EVP_MANAGER_H
#define NEXUS_LLC_CHACHA20_EVP_MANAGER_H

#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <mutex>
#include <openssl/evp.h>

namespace LLC
{
    /** ChaCha20EvpManager
     *
     *  Centralized ChaCha20-Poly1305 AEAD lifecycle manager for node-side
     *  mining protocol crypto operations.
     *
     *  Owns:
     *   - A 32-byte session key tied to one miner's session.
     *   - A 64-bit monotonic nonce counter (IETF nonce format, RFC 8439).
     *   - Typed decrypt/encrypt helpers for each mining protocol message.
     *
     *  Thread safety: all public methods are mutex-protected.
     *  Lifetime: one instance per authenticated miner connection.
     *
     **/
    class ChaCha20EvpManager
    {
    public:
        static constexpr size_t NONCE_SIZE = 12;
        static constexpr size_t TAG_SIZE   = 16;
        static constexpr size_t OVERHEAD   = NONCE_SIZE + TAG_SIZE;
        static constexpr size_t KEY_SIZE   = 32;

        /* AAD domain tag byte vectors (must match miner-side exactly) */
        static const std::vector<uint8_t> AAD_FALCON_PUBKEY;
        static const std::vector<uint8_t> AAD_SUBMIT_BLOCK;    // empty
        static const std::vector<uint8_t> AAD_REWARD_ADDRESS;
        static const std::vector<uint8_t> AAD_REWARD_RESULT;
        static const std::vector<uint8_t> AAD_SESSION_ID;

        struct CryptoResult
        {
            bool success{false};
            std::vector<uint8_t> data;
            std::string error_message;
        };

        explicit ChaCha20EvpManager() = default;
        ~ChaCha20EvpManager() = default;

        ChaCha20EvpManager(const ChaCha20EvpManager&) = delete;
        ChaCha20EvpManager& operator=(const ChaCha20EvpManager&) = delete;

        /** SetSessionKey
         *
         *  Set session key (must be 32 bytes). Resets nonce counter.
         *
         *  @param[in] vKey The 32-byte session key
         *
         **/
        void SetSessionKey(const std::vector<uint8_t>& vKey);

        /** HasSessionKey
         *
         *  Returns true if a 32-byte key is loaded.
         *
         **/
        bool HasSessionKey() const;

        /** Clear
         *
         *  Erase key and reset counter. Call on session teardown.
         *
         **/
        void Clear();

        /** GetNonceCounter
         *
         *  Current nonce counter (diagnostic).
         *
         **/
        uint64_t GetNonceCounter() const;

        /** DecryptPubkey
         *
         *  Decrypt ChaCha20-wrapped Falcon public key.
         *
         *  @param[in]  vPacked [nonce(12)][ciphertext][tag(16)]
         *  @param[in]  vAAD    Must be AAD_FALCON_PUBKEY
         *  @param[out] vOut    Decrypted pubkey on success
         *
         *  @return true on success
         *
         **/
        bool DecryptPubkey(
            const std::vector<uint8_t>& vPacked,
            const std::vector<uint8_t>& vAAD,
            std::vector<uint8_t>& vOut);

        /** DecryptSubmitBlock
         *
         *  Decrypt SUBMIT_BLOCK payload (empty AAD).
         *
         *  @param[in]  vPacked [nonce(12)][ciphertext][tag(16)]
         *  @param[out] vOut    Decrypted plaintext on success
         *
         *  @return true on success
         *
         **/
        bool DecryptSubmitBlock(
            const std::vector<uint8_t>& vPacked,
            std::vector<uint8_t>& vOut);

        /** DecryptRewardAddress
         *
         *  Decrypt MINER_SET_REWARD reward-address payload (AAD "REWARD_ADDRESS").
         *
         *  @param[in]  vPacked [nonce(12)][ciphertext][tag(16)]
         *  @param[out] vOut    Decrypted plaintext on success
         *
         *  @return true on success
         *
         **/
        bool DecryptRewardAddress(
            const std::vector<uint8_t>& vPacked,
            std::vector<uint8_t>& vOut);

        /** EncryptRewardResult
         *
         *  Encrypt MINER_REWARD_RESULT response (AAD "REWARD_RESULT").
         *
         *  @param[in] vPlaintext The plaintext to encrypt
         *
         *  @return CryptoResult with packed [nonce(12)][ciphertext][tag(16)] on success
         *
         **/
        CryptoResult EncryptRewardResult(const std::vector<uint8_t>& vPlaintext);

        /** EncryptSessionId
         *
         *  Encrypt 4-byte session ID (LE uint32) with AAD "SESSION_ID".
         *
         *  @param[in] nSessionId The session ID to encrypt
         *
         *  @return CryptoResult with 32-byte packed: [nonce(12)][ciphertext(4)][tag(16)]
         *
         **/
        CryptoResult EncryptSessionId(uint32_t nSessionId);

        /** DecryptSessionId
         *
         *  Decrypt encrypted session ID.
         *
         *  @param[in]  vPacked  32 bytes: [nonce(12)][ciphertext(4)][tag(16)]
         *  @param[out] nOut     Decrypted session ID on success
         *
         *  @return true on success
         *
         **/
        bool DecryptSessionId(
            const std::vector<uint8_t>& vPacked,
            uint32_t& nOut);

    private:
        CryptoResult EncryptInternal(
            const std::vector<uint8_t>& vPlaintext,
            const std::vector<uint8_t>& vAAD);

        bool DecryptInternal(
            const std::vector<uint8_t>& vPacked,
            const std::vector<uint8_t>& vAAD,
            std::vector<uint8_t>& vOut);

        std::vector<uint8_t> NextNonce();

        mutable std::mutex m_mutex;
        std::vector<uint8_t> m_vKey;
        uint64_t m_nNonceCounter{0};
    };

} // namespace LLC

#endif // NEXUS_LLC_CHACHA20_EVP_MANAGER_H
