#ifndef XCHACHA20_POLY1305_H
#define XCHACHA20_POLY1305_H

#include <sodium.h>
#include <vector>
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace CRYPTO {

    class XChaCha20Poly1305 {
    public:
        static constexpr size_t KEY_BYTES = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
        static constexpr size_t NONCE_BYTES = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
        static constexpr size_t TAG_BYTES = crypto_aead_xchacha20poly1305_ietf_ABYTES;
        static constexpr size_t SALT_BYTES = crypto_pwhash_SALTBYTES;

        static bool is_supported() noexcept {
            return sodium_init() >= 0;
        }

        static std::vector<uint8_t> encrypt(
            std::string_view plaintext,
            std::string_view key_or_password,
            std::string_view salt,
            std::string_view associated_data = ""
        ) {
            ensure_initialized();
            if (salt.empty()) throw std::invalid_argument("Salt cannot be empty.");

            uint8_t derived_key[KEY_BYTES];
            derive_key(key_or_password, salt, derived_key);

            uint8_t nonce[NONCE_BYTES];
            randombytes_buf(nonce, sizeof(nonce));

            size_t ciphertext_len = plaintext.size() + TAG_BYTES;
            std::vector<uint8_t> payload(NONCE_BYTES + ciphertext_len);
            std::memcpy(payload.data(), nonce, NONCE_BYTES);

            unsigned long long actual_clen = 0;
            int ret = crypto_aead_xchacha20poly1305_ietf_encrypt(
                payload.data() + NONCE_BYTES, &actual_clen,
                reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
                reinterpret_cast<const unsigned char*>(associated_data.data()), associated_data.size(),
                nullptr, nonce, derived_key
            );

            sodium_memzero(derived_key, sizeof(derived_key));
            if (ret != 0) throw std::runtime_error("Encryption failed.");

            payload.resize(NONCE_BYTES + actual_clen);
            return payload;
        }

        static std::vector<uint8_t> decrypt(
            const std::vector<uint8_t>& payload,
            std::string_view key_or_password,
            std::string_view salt,
            std::string_view associated_data = ""
        ) {
            ensure_initialized();
            if (salt.empty()) throw std::invalid_argument("Salt cannot be empty.");

            const size_t min_payload_len = NONCE_BYTES + TAG_BYTES;
            if (payload.size() < min_payload_len) throw std::invalid_argument("Payload too short.");

            uint8_t derived_key[KEY_BYTES];
            derive_key(key_or_password, salt, derived_key);

            const uint8_t* nonce_ptr = payload.data();
            const uint8_t* cipher_ptr = payload.data() + NONCE_BYTES;
            size_t cipher_len = payload.size() - NONCE_BYTES;

            std::vector<uint8_t> plaintext(cipher_len - TAG_BYTES);
            unsigned long long actual_mlen = 0;

            int ret = crypto_aead_xchacha20poly1305_ietf_decrypt(
                plaintext.data(), &actual_mlen,
                nullptr,
                cipher_ptr, cipher_len,
                reinterpret_cast<const unsigned char*>(associated_data.data()), associated_data.size(),
                nonce_ptr, derived_key
            );

            sodium_memzero(derived_key, sizeof(derived_key));
            if (ret != 0) throw std::runtime_error("Decryption failed.");

            plaintext.resize(actual_mlen);
            return plaintext;
        }

        static std::string decrypt_to_string(
            const std::vector<uint8_t>& payload,
            std::string_view key_or_password,
            std::string_view salt,
            std::string_view associated_data = ""
        ) {
            auto pt_bytes = decrypt(payload, key_or_password, salt, associated_data);
            return std::string(pt_bytes.begin(), pt_bytes.end());
        }

    private:
        static void ensure_initialized() {
            if (sodium_init() < 0) throw std::runtime_error("Libsodium init failed.");
        }

        static void derive_key(std::string_view key_or_password, std::string_view salt, uint8_t out_key[KEY_BYTES]) {
            uint8_t salt_buf[SALT_BYTES] = { 0 };
            size_t copy_len = (std::min)(salt.size(), static_cast<size_t>(SALT_BYTES));
            std::memcpy(salt_buf, salt.data(), copy_len);

            int ret = crypto_pwhash(
                out_key, KEY_BYTES,
                key_or_password.data(), key_or_password.size(),
                salt_buf,
                8,                                  // 8 次迭代
                1073741824ULL,                      // 1 GB
                crypto_pwhash_ALG_ARGON2ID13        // Argon2id
            );

            sodium_memzero(salt_buf, sizeof(salt_buf));
            if (ret != 0) throw std::runtime_error("Key derivation failed.");
        }
    };

} // namespace CRYPTO

#endif // XCHACHA20_POLY1305_H