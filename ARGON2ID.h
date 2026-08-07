#pragma once

#include <sodium.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <utility>

class Argon2id final {
public:
    static constexpr uint64_t OPS_LIMIT = 4;
    static constexpr size_t   MEM_LIMIT = 2048ULL * 1024ULL * 1024ULL;//2GB
    static constexpr size_t   KEY_LEN = 64;
    static constexpr size_t   SALT_LEN = crypto_pwhash_SALTBYTES;
    static constexpr int      ALG = crypto_pwhash_ALG_ARGON2ID13;

    Argon2id(const std::string& password, const std::vector<unsigned char>& salt) {
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium initialization failed");
        }

        if (password.empty()) {
            throw std::invalid_argument("Password cannot be empty");
        }

        if (salt.size() != SALT_LEN) {
            throw std::invalid_argument("Salt size must be exactly 16 bytes");
        }

        m_password.assign(password.begin(), password.end());
        m_salt = salt;
        m_key.resize(KEY_LEN);

        try {
            lock_memory(m_password);
            lock_memory(m_salt);
            lock_memory(m_key);
            m_locked = true;
        }
        catch (...) {
            cleanup();
            throw;
        }
    }

    ~Argon2id() noexcept {
        cleanup();
    }

    Argon2id(const Argon2id&) = delete;
    Argon2id& operator=(const Argon2id&) = delete;

    Argon2id(Argon2id&& other) noexcept
        : m_password(std::move(other.m_password)),
        m_salt(std::move(other.m_salt)),
        m_key(std::move(other.m_key)),
        m_locked(other.m_locked) {
        other.m_locked = false;
    }

    Argon2id& operator=(Argon2id&& other) noexcept {
        if (this != &other) {
            cleanup();
            m_password = std::move(other.m_password);
            m_salt = std::move(other.m_salt);
            m_key = std::move(other.m_key);
            m_locked = other.m_locked;
            other.m_locked = false;
        }
        return *this;
    }

    const std::vector<unsigned char>& derive_binary() {
        int result = crypto_pwhash(
            m_key.data(), m_key.size(),
            reinterpret_cast<const char*>(m_password.data()), m_password.size(),
            m_salt.data(),
            OPS_LIMIT,
            MEM_LIMIT,
            ALG
        );

        if (result != 0) {
            throw std::runtime_error("Argon2id derivation failed! System memory might be insufficient (< 2048 MiB)");
        }

        return m_key;
    }

    std::string derive_hash_string() const {
        std::vector<char> buf(crypto_pwhash_STRBYTES);

        sodium_mlock(buf.data(), buf.size());

        int result = crypto_pwhash_str(
            buf.data(),
            reinterpret_cast<const char*>(m_password.data()), m_password.size(),
            OPS_LIMIT,
            MEM_LIMIT
        );

        if (result != 0) {
            sodium_memzero(buf.data(), buf.size());
            sodium_munlock(buf.data(), buf.size());
            throw std::runtime_error("Argon2id hash string generation failed");
        }

        std::string result_str(buf.data());

        sodium_memzero(buf.data(), buf.size());
        sodium_munlock(buf.data(), buf.size());

        return result_str;
    }

    static bool verify(const std::string& hash, const std::string& password) noexcept {
        if (sodium_init() < 0) return false;
        return crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.size()) == 0;
    }

    static std::string to_hex(const std::vector<unsigned char>& data) {
        static const char hex_chars[] = "0123456789abcdef";
        std::string s;
        s.reserve(data.size() * 2);
        for (unsigned char b : data) {
            s.push_back(hex_chars[b >> 4]);
            s.push_back(hex_chars[b & 0x0F]);
        }
        return s;
    }

private:
    std::vector<unsigned char> m_password;
    std::vector<unsigned char> m_salt;
    std::vector<unsigned char> m_key;
    bool m_locked = false;

    static void lock_memory(std::vector<unsigned char>& v) {
        if (v.empty()) return;
        if (sodium_mlock(v.data(), v.size()) != 0) {
            throw std::runtime_error("Memory locking failed (sodium_mlock)");
        }
    }

    void cleanup() noexcept {
        secure_clean(m_password);
        secure_clean(m_salt);
        secure_clean(m_key);
        m_locked = false;
    }

    void secure_clean(std::vector<unsigned char>& v) const noexcept {
        if (!v.empty()) {
            sodium_memzero(v.data(), v.size());
            if (m_locked) {
                sodium_munlock(v.data(), v.size());
            }
        }
    }
};