#include <string>
#include <array>
#include <cstdint>
#include <type_traits>
#include <windows.h> // 提供 SecureZeroMemory

/**
 * @brief 编译期 10 重混淆 + 运行期即时解密 (无类实现，直接返回 std::string / std::wstring)
 *
 * @param str_literal 字符串字面量 (例如 "MyPassword" 或 L"MyApp/MyUser")
 * @return std::string 或 std::wstring
 */
#define OBFUSCATE_STR(str_literal) []() { \
    using CharT = typename std::remove_cv<typename std::remove_reference<decltype(str_literal[0])>::type>::type; \
    constexpr size_t N = sizeof(str_literal) / sizeof(CharT); \
    constexpr size_t ByteSize = sizeof(str_literal); \
    constexpr uint32_t Seed = static_cast<uint32_t>(__LINE__ * 2654435761u + __COUNTER__); \
    \
    /* 1. 【编译期】10 重正向混淆：生成无规则字节数组 */ \
    constexpr auto enc_bytes = [=]() constexpr { \
        std::array<uint8_t, ByteSize> bytes{}; \
        uint32_t lcg_state = Seed; \
        uint8_t prev_cipher = 0xA5; \
        for (size_t i = 0; i < ByteSize; ++i) { \
            size_t char_idx = i / sizeof(CharT); \
            size_t byte_shift = (i % sizeof(CharT)) * 8; \
            uint8_t x = static_cast<uint8_t>((str_literal[char_idx] >> byte_shift) & 0xFF); \
            /* 1. S-Box 仿射代换 */ x = static_cast<uint8_t>((x * 151 + 0x37) ^ 0x6A); \
            /* 2. 高低 4 位翻转  */ x = static_cast<uint8_t>((x << 4) | (x >> 4)); \
            /* 3. 动态位置异或  */ x = static_cast<uint8_t>(x ^ (i * 0x37 + 0x4B)); \
            /* 4. 循环左移 3 位  */ x = static_cast<uint8_t>((x << 3) | (x >> 5)); \
            /* 5. 模加偏移      */ x = static_cast<uint8_t>(x + (i * 13 + 0x2E)); \
            /* 6. 按位取反      */ x = static_cast<uint8_t>(~x); \
            /* 7. CBC 密文级联   */ x = static_cast<uint8_t>(x ^ prev_cipher); prev_cipher = x; \
            /* 8. LCG 随机流异或 */ lcg_state = static_cast<uint32_t>(lcg_state * 1664525u + 1013904223u); x = static_cast<uint8_t>(x ^ (lcg_state >> 24)); \
            /* 9. 循环右移 2 位  */ x = static_cast<uint8_t>((x >> 2) | (x << 6)); \
            /* 10. 全局常量异或  */ x = static_cast<uint8_t>(x ^ 0xD7); \
            bytes[i] = x; \
        } \
        return bytes; \
    }(); \
    \
    /* 2. 【运行期】10 重逆向解密：还原并组装为 std::string / std::wstring */ \
    std::basic_string<CharT> res(N - 1, CharT(0)); \
    uint8_t raw_dst[ByteSize]{}; \
    uint32_t lcg_state = Seed; \
    uint8_t prev_cipher = 0xA5; \
    for (size_t i = 0; i < ByteSize; ++i) { \
        uint8_t x = enc_bytes[i]; \
        /* 逆 10 */ x = static_cast<uint8_t>(x ^ 0xD7); \
        /* 逆 9  */ x = static_cast<uint8_t>((x << 2) | (x >> 6)); \
        /* 逆 8  */ lcg_state = static_cast<uint32_t>(lcg_state * 1664525u + 1013904223u); x = static_cast<uint8_t>(x ^ (lcg_state >> 24)); \
        /* 逆 7  */ uint8_t current_step7 = x; x = static_cast<uint8_t>(x ^ prev_cipher); prev_cipher = current_step7; \
        /* 逆 6  */ x = static_cast<uint8_t>(~x); \
        /* 逆 5  */ x = static_cast<uint8_t>(x - (i * 13 + 0x2E)); \
        /* 逆 4  */ x = static_cast<uint8_t>((x >> 3) | (x << 5)); \
        /* 逆 3  */ x = static_cast<uint8_t>(x ^ (i * 0x37 + 0x4B)); \
        /* 逆 2  */ x = static_cast<uint8_t>((x << 4) | (x >> 4)); \
        /* 逆 1  */ x = static_cast<uint8_t>(((x ^ 0x6A) - 0x37) * 39); \
        raw_dst[i] = x; \
    } \
    for (size_t i = 0; i < N - 1; ++i) { \
        CharT c = 0; \
        for (size_t b = 0; b < sizeof(CharT); ++b) { \
            c |= (static_cast<CharT>(raw_dst[i * sizeof(CharT) + b]) << (b * 8)); \
        } \
        res[i] = c; \
    } \
    /* 3. 安全擦除解密栈缓冲区 */ \
    SecureZeroMemory(raw_dst, ByteSize); \
    return res; \
}()