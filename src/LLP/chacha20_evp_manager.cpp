/*__________________________________________________________________________________________

            Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014]++

            (c) Copyright The Nexus Developers 2014 - 2025

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLP/include/chacha20_evp_manager.h>

#include <LLC/include/chacha20_helpers.h>

#include <Util/include/debug.h>
#include <Util/include/config.h>

namespace LLP
{

    /* static singleton accessor */
    Chacha20EvpManager& Chacha20EvpManager::Get()
    {
        static Chacha20EvpManager instance;
        return instance;
    }


    /* Called once at mining server startup. Idempotent. */
    void Chacha20EvpManager::Initialize()
    {
        /* Guard against re-initialization: only the first call takes effect */
        bool fExpected = false;
        if(!m_initialized.compare_exchange_strong(fExpected, true))
            return;

        /* Read config flags.
         * -miningevp  (default true)  — ChaCha20-Poly1305 EVP mode
         * -miningtls  (default false) — TLS mode (future, opt-in) */
        const bool fEvp = config::GetBoolArg(std::string("-miningevp"), true);
        const bool fTls = config::GetBoolArg(std::string("-miningtls"), false);

        /* Mutual exclusion: EVP and TLS cannot both be active on a single node.
         * If both flags are set, EVP wins and we emit a warning. */
        if(fEvp && fTls)
        {
            debug::log(0, FUNCTION, ANSI_COLOR_BRIGHT_YELLOW,
                "WARNING: Both -miningevp and -miningtls are set. EVP and TLS are mutually exclusive -- EVP (ChaCha20-Poly1305) will be used.",
                ANSI_COLOR_RESET);
            m_mode.store(MiningTransportMode::EVP);
        }
        else if(fTls && !fEvp)
        {
            m_mode.store(MiningTransportMode::TLS);
        }
        else
        {
            /* Default: EVP is always the fallback when TLS is not explicitly requested */
            m_mode.store(MiningTransportMode::EVP);
        }

        debug::log(0, FUNCTION, "Mining transport mode: ", ModeString());
    }


    /* Query current mode */
    MiningTransportMode Chacha20EvpManager::GetMode() const
    {
        return m_mode.load();
    }


    /* Check if EVP is active */
    bool Chacha20EvpManager::IsEvpActive() const
    {
        return m_mode.load() == MiningTransportMode::EVP;
    }


    /* Check if TLS is active */
    bool Chacha20EvpManager::IsTlsActive() const
    {
        return m_mode.load() == MiningTransportMode::TLS;
    }


    /* Returns true when remote miners MUST use encrypted packets */
    bool Chacha20EvpManager::IsEncryptionRequired() const
    {
        /* EVP mode mandates encryption for all remote peers */
        return m_mode.load() == MiningTransportMode::EVP;
    }


    /* Encrypt plaintext using the active transport mode */
    bool Chacha20EvpManager::Encrypt(
        const std::vector<uint8_t>& vPlain,
        const std::vector<uint8_t>& vKey,
        std::vector<uint8_t>& vOut) const
    {
        const MiningTransportMode eMode = m_mode.load();

        if(eMode == MiningTransportMode::EVP)
        {
            /* Delegate to the LLC ChaCha20-Poly1305 helper */
            vOut = LLC::EncryptPayloadChaCha20(vPlain, vKey);
            return !vOut.empty();
        }

        if(eMode == MiningTransportMode::TLS)
        {
            /* TLS encryption is not yet implemented — future stub */
            debug::error(FUNCTION, "TLS encrypt called but TLS is not yet implemented");
            return false;
        }

        /* NONE mode — no encryption (only valid for localhost) */
        debug::error(FUNCTION, "Encrypt called with NONE mode — this should not happen for remote peers");
        return false;
    }


    /* Decrypt ciphertext using the active transport mode */
    bool Chacha20EvpManager::Decrypt(
        const std::vector<uint8_t>& vCipher,
        const std::vector<uint8_t>& vKey,
        std::vector<uint8_t>& vOut) const
    {
        const MiningTransportMode eMode = m_mode.load();

        if(eMode == MiningTransportMode::EVP)
        {
            /* Delegate to the LLC ChaCha20-Poly1305 helper */
            return LLC::DecryptPayloadChaCha20(vCipher, vKey, vOut);
        }

        if(eMode == MiningTransportMode::TLS)
        {
            /* TLS decryption is not yet implemented — future stub */
            debug::error(FUNCTION, "TLS decrypt called but TLS is not yet implemented");
            return false;
        }

        /* NONE mode */
        debug::error(FUNCTION, "Decrypt called with NONE mode — this should not happen for remote peers");
        return false;
    }


    /* Gate check: returns true only for localhost when encryption is not required */
    bool Chacha20EvpManager::AllowPlaintext(const std::string& strPeerIP) const
    {
        /* Plaintext is only acceptable for localhost miners when encryption is off */
        return (strPeerIP == "127.0.0.1") && !IsEncryptionRequired();
    }


    /* Returns human-readable mode string */
    std::string Chacha20EvpManager::ModeString() const
    {
        switch(m_mode.load())
        {
            case MiningTransportMode::EVP:
                return "EVP (ChaCha20-Poly1305)";
            case MiningTransportMode::TLS:
                return "TLS";
            case MiningTransportMode::NONE:
                return "NONE";
            default:
                return "UNKNOWN";
        }
    }


    /* Prune any cached session material for sessions no longer in the live miner map */
    void Chacha20EvpManager::prune_expired_sessions([[maybe_unused]] const std::vector<uint32_t>& live_session_ids)
    {
        /* The current implementation delegates all per-session key material to the
         * LLC helpers (keys are held by MiningContext, not the manager). There is no
         * internal session key store to prune at this time. This method exists as the
         * stable hook point: if a future version adds an internal session key cache,
         * it should iterate that cache here and remove any entry whose session ID is
         * not found in live_session_ids. */
    }

} // namespace LLP
