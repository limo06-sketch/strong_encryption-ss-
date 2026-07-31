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
        // 常量定义
        static constexpr size_t KEY_BYTES = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;   // 32 字节
        static constexpr size_t NONCE_BYTES = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;  // 24 字节 (XChaCha20 扩展 Nonce)
        static constexpr size_t TAG_BYTES = crypto_aead_xchacha20poly1305_ietf_ABYTES;     // 16 字节
        static constexpr size_t SALT_BYTES = crypto_pwhash_SALTBYTES;                       // 16 字节

        /**
         * @brief 初始化环境并检查支持
         * XChaCha20-Poly1305 为软件实现，只要 libsodium 成功初始化即可支持
         */
        static bool is_supported() noexcept {
            return sodium_init() >= 0;
        }

        /**
         * @brief 使用密钥/密码与盐加密数据
         *
         * @param plaintext 明文内容
         * @param key_or_password 用户输入的密钥或密码
         * @param salt 用户输入的盐 (建议长度 >= 16 字节)
         * @param associated_data 附加验证数据 AAD (可选)
         * @return std::vector<uint8_t> 加密结果 Payload，结构为: [ 24B Nonce ] + [ 密文 + 16B Tag ]
         */
        static std::vector<uint8_t> encrypt(
            std::string_view plaintext,
            std::string_view key_or_password,
            std::string_view salt,
            std::string_view associated_data = ""
        ) {
            ensure_initialized();

            if (salt.empty()) {
                throw std::invalid_argument("Salt cannot be empty.");
            }

            // 1. 利用 Argon2id 根据密钥与盐派生 32 字节密钥
            uint8_t derived_key[KEY_BYTES];
            derive_key(key_or_password, salt, derived_key);

            // 2. 生成 24 字节强随机 Nonce (XChaCha20 的 Nonce 足够长，可以安全地随机生成)
            uint8_t nonce[NONCE_BYTES];
            randombytes_buf(nonce, sizeof(nonce));

            // 3. 分配 Payload 空间：Nonce (24B) + Plaintext + Tag (16B)
            size_t ciphertext_len = plaintext.size() + TAG_BYTES;
            std::vector<uint8_t> payload(NONCE_BYTES + ciphertext_len);

            // 拷贝 Nonce 到 Payload 头部
            std::memcpy(payload.data(), nonce, NONCE_BYTES);

            // 4. 执行 XChaCha20-Poly1305 加密
            unsigned long long actual_clen = 0;
            int ret = crypto_aead_xchacha20poly1305_ietf_encrypt(
                payload.data() + NONCE_BYTES, &actual_clen,
                reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
                reinterpret_cast<const unsigned char*>(associated_data.data()), associated_data.size(),
                nullptr, nonce, derived_key
            );

            // 擦除内存敏感数据
            sodium_memzero(derived_key, sizeof(derived_key));

            if (ret != 0) {
                throw std::runtime_error("XChaCha20-Poly1305 encryption operation failed.");
            }

            payload.resize(NONCE_BYTES + actual_clen);
            return payload;
        }

        /**
         * @brief 使用密钥/密码与盐解密数据
         *
         * @param payload 密文数据包 (格式: [ 24B Nonce ] + [ 密文 + 16B Tag ])
         * @param key_or_password 用户输入的密钥或密码
         * @param salt 用户输入的盐 (需与加密时一致)
         * @param associated_data 附加验证数据 AAD (需与加密时一致)
         * @return std::vector<uint8_t> 解密后的明文字节数组
         */
        static std::vector<uint8_t> decrypt(
            const std::vector<uint8_t>& payload,
            std::string_view key_or_password,
            std::string_view salt,
            std::string_view associated_data = ""
        ) {
            ensure_initialized();

            if (salt.empty()) {
                throw std::invalid_argument("Salt cannot be empty.");
            }

            const size_t min_payload_len = NONCE_BYTES + TAG_BYTES;
            if (payload.size() < min_payload_len) {
                throw std::invalid_argument("Payload length is too short or corrupted.");
            }

            // 1. 派生密钥
            uint8_t derived_key[KEY_BYTES];
            derive_key(key_or_password, salt, derived_key);

            // 2. 提取 Nonce 与密文指针
            const uint8_t* nonce_ptr = payload.data();
            const uint8_t* cipher_ptr = payload.data() + NONCE_BYTES;
            size_t cipher_len = payload.size() - NONCE_BYTES;

            // 3. 分配明文接收空间
            std::vector<uint8_t> plaintext(cipher_len - TAG_BYTES);
            unsigned long long actual_mlen = 0;

            // 4. 执行 XChaCha20-Poly1305 解密与标签校验
            int ret = crypto_aead_xchacha20poly1305_ietf_decrypt(
                plaintext.data(), &actual_mlen,
                nullptr,
                cipher_ptr, cipher_len,
                reinterpret_cast<const unsigned char*>(associated_data.data()), associated_data.size(),
                nonce_ptr, derived_key
            );

            // 擦除内存敏感数据
            sodium_memzero(derived_key, sizeof(derived_key));

            if (ret != 0) {
                throw std::runtime_error("XChaCha20-Poly1305 decryption failed: invalid key, salt, or tampered payload.");
            }

            plaintext.resize(actual_mlen);
            return plaintext;
        }

        /**
         * @brief 便捷解密函数，直接返回 std::string 文本
         */
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
            if (sodium_init() < 0) {
                throw std::runtime_error("Failed to initialize Libsodium.");
            }
        }

        static void derive_key(std::string_view key_or_password, std::string_view salt, uint8_t out_key[KEY_BYTES]) {
            uint8_t salt_buf[SALT_BYTES] = { 0 };
            size_t copy_len = (std::min)(salt.size(), static_cast<size_t>(SALT_BYTES));
            std::memcpy(salt_buf, salt.data(), copy_len);

            int ret = crypto_pwhash(
                out_key, KEY_BYTES,
                key_or_password.data(), key_or_password.size(),
                salt_buf,
                crypto_pwhash_OPSLIMIT_INTERACTIVE,
                crypto_pwhash_MEMLIMIT_INTERACTIVE,
                crypto_pwhash_ALG_DEFAULT
            );

            sodium_memzero(salt_buf, sizeof(salt_buf));

            if (ret != 0) {
                throw std::runtime_error("Argon2id key derivation failed (Memory/System limit error).");
            }
        }
    };

} // namespace CRYPTO

#endif // XCHACHA20_POLY1305_H