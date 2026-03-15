/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLC/include/chacha20_evp_manager.h>
#include <Util/include/debug.h>

#include <openssl/evp.h>
#include <cstring>
#include <stdexcept>

namespace LLC
{
    /* AAD domain tag constants — must match miner-side byte-for-byte */
    const std::vector<uint8_t> ChaCha20EvpManager::AAD_FALCON_PUBKEY =
        {'F','A','L','C','O','N','_','P','U','B','K','E','Y'};

    const std::vector<uint8_t> ChaCha20EvpManager::AAD_SUBMIT_BLOCK = {};

    const std::vector<uint8_t> ChaCha20EvpManager::AAD_REWARD_ADDRESS =
        {'R','E','W','A','R','D','_','A','D','D','R','E','S','S'};

    const std::vector<uint8_t> ChaCha20EvpManager::AAD_REWARD_RESULT =
        {'R','E','W','A','R','D','_','R','E','S','U','L','T'};

    const std::vector<uint8_t> ChaCha20EvpManager::AAD_SESSION_ID =
        {'S','E','S','S','I','O','N','_','I','D'};


    /* Set session key (must be 32 bytes). Resets nonce counter. */
    void ChaCha20EvpManager::SetSessionKey(const std::vector<uint8_t>& vKey)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if(vKey.size() != KEY_SIZE)
        {
            debug::error(FUNCTION, "ChaCha20EvpManager: key must be 32 bytes, got ", vKey.size());
            return;
        }

        m_vKey = vKey;
        m_nNonceCounter = 0;
    }


    /* Returns true if a 32-byte key is loaded. */
    bool ChaCha20EvpManager::HasSessionKey() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_vKey.size() == KEY_SIZE;
    }


    /* Erase key and reset counter. Call on session teardown. */
    void ChaCha20EvpManager::Clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        /* Securely zero the key material before clearing */
        if(!m_vKey.empty())
        {
            std::memset(m_vKey.data(), 0, m_vKey.size());
            m_vKey.clear();
        }
        m_nNonceCounter = 0;
    }


    /* Current nonce counter (diagnostic). */
    uint64_t ChaCha20EvpManager::GetNonceCounter() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_nNonceCounter;
    }


    /* Build the next 12-byte IETF nonce: [0x00 * 4][counter as 8-byte LE] */
    std::vector<uint8_t> ChaCha20EvpManager::NextNonce()
    {
        /* Called with m_mutex already held */
        std::vector<uint8_t> vNonce(NONCE_SIZE, 0x00);

        /* Write 8-byte little-endian counter at bytes [4..11] */
        uint64_t counter = m_nNonceCounter++;
        for(int i = 0; i < 8; ++i)
            vNonce[4 + i] = static_cast<uint8_t>((counter >> (8 * i)) & 0xFF);

        return vNonce;
    }


    /* Encrypt with ChaCha20-Poly1305. Returns [nonce(12)][ciphertext][tag(16)]. */
    ChaCha20EvpManager::CryptoResult ChaCha20EvpManager::EncryptInternal(
        const std::vector<uint8_t>& vPlaintext,
        const std::vector<uint8_t>& vAAD)
    {
        /* Called with m_mutex already held */
        CryptoResult result;

        if(m_vKey.size() != KEY_SIZE)
        {
            result.error_message = "No session key loaded";
            return result;
        }

        if(vPlaintext.empty())
        {
            result.error_message = "Cannot encrypt empty plaintext";
            return result;
        }

        /* Build monotonic nonce */
        const std::vector<uint8_t> vNonce = NextNonce();

        /* Create cipher context */
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if(!ctx)
        {
            result.error_message = "Failed to create EVP cipher context";
            return result;
        }

        /* Initialize encryption */
        if(EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, m_vKey.data(), vNonce.data()) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            result.error_message = "Failed to initialize ChaCha20-Poly1305 encryption";
            return result;
        }

        /* Set AAD if provided */
        if(!vAAD.empty())
        {
            int nLen = 0;
            if(EVP_EncryptUpdate(ctx, nullptr, &nLen, vAAD.data(), static_cast<int>(vAAD.size())) != 1)
            {
                EVP_CIPHER_CTX_free(ctx);
                result.error_message = "Failed to set AAD";
                return result;
            }
        }

        /* Prepare ciphertext buffer */
        std::vector<uint8_t> vCiphertext(vPlaintext.size());
        int nCiphertextLen = 0;

        /* Perform encryption */
        if(EVP_EncryptUpdate(ctx, vCiphertext.data(), &nCiphertextLen,
                             vPlaintext.data(), static_cast<int>(vPlaintext.size())) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            result.error_message = "ChaCha20-Poly1305 encryption failed";
            return result;
        }

        /* Finalize encryption */
        int nFinalLen = 0;
        if(EVP_EncryptFinal_ex(ctx, vCiphertext.data() + nCiphertextLen, &nFinalLen) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            result.error_message = "ChaCha20-Poly1305 encryption finalization failed";
            return result;
        }

        vCiphertext.resize(nCiphertextLen + nFinalLen);

        /* Extract 16-byte authentication tag */
        std::vector<uint8_t> vTag(TAG_SIZE);
        if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, static_cast<int>(TAG_SIZE), vTag.data()) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            result.error_message = "Failed to get authentication tag";
            return result;
        }

        EVP_CIPHER_CTX_free(ctx);

        /* Pack: [nonce(12)][ciphertext][tag(16)] */
        result.data.reserve(NONCE_SIZE + vCiphertext.size() + TAG_SIZE);
        result.data.insert(result.data.end(), vNonce.begin(), vNonce.end());
        result.data.insert(result.data.end(), vCiphertext.begin(), vCiphertext.end());
        result.data.insert(result.data.end(), vTag.begin(), vTag.end());
        result.success = true;

        return result;
    }


    /* Decrypt packed [nonce(12)][ciphertext][tag(16)] format. */
    bool ChaCha20EvpManager::DecryptInternal(
        const std::vector<uint8_t>& vPacked,
        const std::vector<uint8_t>& vAAD,
        std::vector<uint8_t>& vOut)
    {
        /* Called with m_mutex already held */

        if(m_vKey.size() != KEY_SIZE)
        {
            debug::error(FUNCTION, "ChaCha20EvpManager: no session key loaded");
            return false;
        }

        /* Minimum: nonce(12) + at least 1 ciphertext byte + tag(16) = 29 bytes */
        if(vPacked.size() < OVERHEAD + 1)
        {
            debug::error(FUNCTION, "ChaCha20EvpManager: packed payload too small: ", vPacked.size());
            return false;
        }

        /* Split packed buffer */
        const std::vector<uint8_t> vNonce(vPacked.begin(), vPacked.begin() + NONCE_SIZE);
        const std::vector<uint8_t> vTag(vPacked.end() - TAG_SIZE, vPacked.end());
        const std::vector<uint8_t> vCiphertext(vPacked.begin() + NONCE_SIZE, vPacked.end() - TAG_SIZE);

        /* Create cipher context */
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if(!ctx)
        {
            debug::error(FUNCTION, "ChaCha20EvpManager: failed to create EVP cipher context");
            return false;
        }

        /* Initialize decryption */
        if(EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, m_vKey.data(), vNonce.data()) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            debug::error(FUNCTION, "ChaCha20EvpManager: failed to initialize decryption");
            return false;
        }

        /* Set AAD if provided */
        if(!vAAD.empty())
        {
            int nLen = 0;
            if(EVP_DecryptUpdate(ctx, nullptr, &nLen, vAAD.data(), static_cast<int>(vAAD.size())) != 1)
            {
                EVP_CIPHER_CTX_free(ctx);
                debug::error(FUNCTION, "ChaCha20EvpManager: failed to set AAD");
                return false;
            }
        }

        /* Perform decryption */
        vOut.resize(vCiphertext.size());
        int nPlaintextLen = 0;

        if(EVP_DecryptUpdate(ctx, vOut.data(), &nPlaintextLen,
                             vCiphertext.data(), static_cast<int>(vCiphertext.size())) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            debug::error(FUNCTION, "ChaCha20EvpManager: decryption failed");
            return false;
        }

        /* Set expected authentication tag */
        if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, static_cast<int>(TAG_SIZE),
                               const_cast<uint8_t*>(vTag.data())) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            debug::error(FUNCTION, "ChaCha20EvpManager: failed to set authentication tag");
            return false;
        }

        /* Verify tag and finalize */
        int nFinalLen = 0;
        if(EVP_DecryptFinal_ex(ctx, vOut.data() + nPlaintextLen, &nFinalLen) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            debug::error(FUNCTION, "ChaCha20EvpManager: authentication tag verification failed");
            return false;
        }

        EVP_CIPHER_CTX_free(ctx);
        vOut.resize(nPlaintextLen + nFinalLen);
        return true;
    }


    /* Decrypt ChaCha20-wrapped Falcon public key. */
    bool ChaCha20EvpManager::DecryptPubkey(
        const std::vector<uint8_t>& vPacked,
        const std::vector<uint8_t>& vAAD,
        std::vector<uint8_t>& vOut)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return DecryptInternal(vPacked, vAAD, vOut);
    }


    /* Decrypt SUBMIT_BLOCK payload (empty AAD). */
    bool ChaCha20EvpManager::DecryptSubmitBlock(
        const std::vector<uint8_t>& vPacked,
        std::vector<uint8_t>& vOut)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return DecryptInternal(vPacked, AAD_SUBMIT_BLOCK, vOut);
    }


    /* Decrypt MINER_SET_REWARD reward-address payload (AAD "REWARD_ADDRESS"). */
    bool ChaCha20EvpManager::DecryptRewardAddress(
        const std::vector<uint8_t>& vPacked,
        std::vector<uint8_t>& vOut)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return DecryptInternal(vPacked, AAD_REWARD_ADDRESS, vOut);
    }


    /* Encrypt MINER_REWARD_RESULT response (AAD "REWARD_RESULT"). */
    ChaCha20EvpManager::CryptoResult ChaCha20EvpManager::EncryptRewardResult(
        const std::vector<uint8_t>& vPlaintext)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return EncryptInternal(vPlaintext, AAD_REWARD_RESULT);
    }


    /* Encrypt 4-byte session ID (LE uint32) with AAD "SESSION_ID". */
    ChaCha20EvpManager::CryptoResult ChaCha20EvpManager::EncryptSessionId(uint32_t nSessionId)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        /* Serialize session ID as 4-byte little-endian */
        std::vector<uint8_t> vPlaintext(4);
        vPlaintext[0] = static_cast<uint8_t>((nSessionId)       & 0xFF);
        vPlaintext[1] = static_cast<uint8_t>((nSessionId >>  8) & 0xFF);
        vPlaintext[2] = static_cast<uint8_t>((nSessionId >> 16) & 0xFF);
        vPlaintext[3] = static_cast<uint8_t>((nSessionId >> 24) & 0xFF);

        return EncryptInternal(vPlaintext, AAD_SESSION_ID);
    }


    /* Decrypt encrypted session ID. */
    bool ChaCha20EvpManager::DecryptSessionId(
        const std::vector<uint8_t>& vPacked,
        uint32_t& nOut)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::vector<uint8_t> vPlaintext;
        if(!DecryptInternal(vPacked, AAD_SESSION_ID, vPlaintext))
            return false;

        if(vPlaintext.size() != 4)
        {
            debug::error(FUNCTION, "ChaCha20EvpManager: decrypted session ID size mismatch: ", vPlaintext.size());
            return false;
        }

        /* Deserialize 4-byte little-endian */
        nOut = static_cast<uint32_t>(vPlaintext[0])
             | (static_cast<uint32_t>(vPlaintext[1]) <<  8)
             | (static_cast<uint32_t>(vPlaintext[2]) << 16)
             | (static_cast<uint32_t>(vPlaintext[3]) << 24);

        return true;
    }

} // namespace LLC
