#include <iostream>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <chrono>
#include <sstream>
#include <fstream>
#include <cwchar>
#include <thread>
#include <limits>
#include <windows.h>

#include "Argon2id.h"
#include "toolkit.h"
#include "ObfuscatedString.h"
#include "XCHACHA20_POLY1305.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

// ==========================================
// 解锁动画
// ==========================================
inline static void play_unlock_animation() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) SetConsoleMode(hOut, mode | 0x0004); // ENABLE_VIRTUAL_TERMINAL_PROCESSING
    }
#endif

    // 隐藏控制台光标
    std::cout << "\033[?25l";

    const char rot_chars[] = { '-', '/', '|', '\\' };

    // 阶段 1：旋转动画
    for (int angle = 0; angle <= 180; angle += 15) {
        std::system("cls");

        char cur_rot = rot_chars[(angle / 15) % 4];
        int percent = (angle * 100) / 180;

        std::cout << "\033[1;33m[!] CRACKING: ENGAGING MECHANICAL KEY...\033[0m\n";
        std::cout << "\033[1;30m[!] TURNING TUMBLER: " << angle << "° / 180° [" << cur_rot << "]\033[0m\n\n";

        std::cout << "\033[1;31m"
            << "       /------\\\n"
            << "      /        \\\n"
            << "     |          |\n"
            << "     |          |\n"
            << "  [--------------]   <-- [ TURNING: " << cur_rot << cur_rot << " ]\n"
            << " [|              |]\n"
            << " [|  CYLINDER " << cur_rot << "  |]\n"
            << " [|              |]\n"
            << " [|--------------|]\n"
            << "\033[0m\n";

        std::cout << "\033[1;33m[ROTATING] [";
        int bar_width = 20;
        int pos = (percent * bar_width) / 100;
        for (int p = 0; p < bar_width; ++p) {
            if (p < pos) std::cout << "=";
            else if (p == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << percent << "%\033[0m\n" << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    // 阶段 2：摆动脱钩
    const char* swing_frames[] = {
        "\033[1;33m[!] MECHANICAL OVERRIDE: TUMBLERS ALIGNED.\033[0m\n"
        "\033[1;33m[!] UNLATCHING LOCK SHACKLE...\033[0m\n\n"
        "\033[1;33m"
        "       /------\\\n"
        "      /        \\\n"
        "     |          |\n"
        "                |\n"
        "  [--------------]   <-- [ UNLATCHED ]\n"
        " [|              |]\n"
        " [|  UNLATCHING  |]\n"
        " [|              |]\n"
        " [|--------------|]\n"
        "\033[0m\n"
        "\033[1;33m[ROTATING] [====================] 100%\033[0m\n",

        "\033[1;33m[!] MECHANICAL OVERRIDE: SWINGING OPEN.\033[0m\n"
        "\033[1;33m[!] SHACKLE ROTATED OUTWARD\033[0m\n\n"
        "\033[1;33m"
        "      /------\\\n"
        "     /        \\\n"
        "    |          |\n"
        "               |\n"
        "  [--          --]   <-- [ SWINGING OPEN ]\n"
        " [|              |]\n"
        " [|   OPENING    |]\n"
        " [|              |]\n"
        " [|--------------|]\n"
        "\033[0m\n"
        "\033[1;32m[SUCCESS] MECHANISM FULLY DISENGAGED\033[0m\n"
    };

    for (int i = 0; i < 2; ++i) {
        std::system("cls");
        std::cout << swing_frames[i] << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 阶段 3：解锁完成
    std::system("cls");
    std::cout << "\033[1;32m[!] WARNING: SYSTEM UNLOCKED! ACCESS GRANTED.\033[0m\n";
    std::cout << "\033[1;31m[!] SECURITY BREACH: LOCK DISENGAGED.\033[0m\n\n";
    std::cout << "\033[1;31m"
        << "       /------\\\n"
        << "      /        \\\n"
        << "     |          |\n"
        << "     |          |\n"
        << "  [------      -----]   <-- [ LOCK OPENED ]\n"
        << " [|                |]\n"
        << " [|                |]\n"
        << " [|                |]\n"
        << " [|----------------|]\n"
        << "\033[0m\n";
    std::cout << "\033[1;32m[SYSTEM ONLINE] Entering Entropy Cracking...\033[0m\n\n" << std::flush;

    // 恢复控制台光标
    std::cout << "\033[?25h";
}

// ==========================================
// 工具辅助函数
// ==========================================
inline static std::vector<unsigned char> generate_argon2_salt() {
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium initialization failed");
    }

    auto clock_val = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );

    std::vector<unsigned char> base_rand(32);
    randombytes_buf(base_rand.data(), base_rand.size());

    std::vector<unsigned char> pool;
    pool.reserve(32 + sizeof(clock_val));
    pool.insert(pool.end(), base_rand.begin(), base_rand.end());

    const auto* cb = reinterpret_cast<const unsigned char*>(&clock_val);
    pool.insert(pool.end(), cb, cb + sizeof(clock_val));

    std::vector<unsigned char> salt(crypto_pwhash_SALTBYTES);
    if (crypto_generichash(salt.data(), salt.size(), pool.data(), pool.size(), nullptr, 0) != 0) {
        sodium_memzero(pool.data(), pool.size());
        sodium_memzero(base_rand.data(), base_rand.size());
        throw std::runtime_error("Entropy pool hashing failed");
    }

    sodium_memzero(pool.data(), pool.size());
    sodium_memzero(base_rand.data(), base_rand.size());

    return salt;
}

static std::string read_txt_to_string(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }
    return std::string(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
}

static std::string vector_to_string(const std::vector<unsigned char>& vec) {
    if (vec.empty()) return std::string();
    return std::string(reinterpret_cast<const char*>(vec.data()), vec.size());
}

static bool write_vector_to_file(const std::string& filepath, const std::vector<uint8_t>& data) {
    std::ofstream file(filepath, std::ios::out | std::ios::binary);
    if (!file.is_open()) return false;
    if (data.empty()) return true;

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return static_cast<bool>(file);
}

inline static std::vector<uint8_t> string_to_bytes(const std::string& str) {
    return std::vector<uint8_t>(str.begin(), str.end());
}

static void sleep_for_seconds(size_t seconds) {
    precise_busy_wait_dual_core(seconds * 1000 * 1000, 2);
}

// ==========================================
// Windows DPAPI & Credential 结构体定义
// ==========================================
typedef struct _MY_DATA_BLOB {
    DWORD cbData;
    BYTE* pbData;
} MY_DATA_BLOB;

typedef struct _MY_CREDENTIALW {
    DWORD Flags; DWORD Type; LPWSTR TargetName; LPWSTR Comment;
    FILETIME LastWritten; DWORD CredentialBlobSize; LPBYTE CredentialBlob;
    DWORD Persist; DWORD AttributeCount; LPVOID Attributes;
    LPWSTR TargetAlias; LPWSTR UserName;
} MY_CREDENTIALW;

constexpr auto MY_CRYPT_STRING_BASE64 = 0x00000001;

static BYTE g_QuantumEntropy[28];

static const struct _QuantumEntropyInit {
    _QuantumEntropyInit() {
        std::string s = OBFUSCATE_STR(
            "\x7F\x3E\x9A\xC4\x1B\xD2\x8E\x5F"
            "\xA1\x6C\x4B\x93\xE7\x2D\x10\x8A"
            "\xBD\xC3\x54\x6F\x2E\x91\x7A\x0B"
            "\x5E\x88\xDF\x1C"
        );
        memcpy(g_QuantumEntropy, s.data(), 28);
        SecureZeroMemory(s.data(), 28);
    }
} _g_QuantumEntropy_init;

typedef BOOL(WINAPI* pfnCredReadW)(LPCWSTR, DWORD, DWORD, MY_CREDENTIALW**);
typedef VOID(WINAPI* pfnCredFree)(LPVOID);
typedef BOOL(WINAPI* pfnCryptStringToBinaryW)(LPCWSTR, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*);
typedef BOOL(WINAPI* pfnCryptUnprotectData)(MY_DATA_BLOB*, LPWSTR*, MY_DATA_BLOB*, LPVOID, LPVOID, DWORD, MY_DATA_BLOB*);

static std::string GetDecryptedSecret_Final(const char* targetName) {
    //std::cout << "\n========== [DECRYPT START] ==========\n";
    if (!targetName || !*targetName) return "";

    int wlen = MultiByteToWideChar(CP_UTF8, 0, targetName, -1, nullptr, 0);
    std::vector<wchar_t> targetW(wlen);
    MultiByteToWideChar(CP_UTF8, 0, targetName, -1, targetW.data(), wlen);
    std::wstring sTarget(targetW.data());

    HMODULE hAdvapi = LoadLibraryW(L"advapi32.dll");
    HMODULE hCrypt = LoadLibraryW(L"crypt32.dll");
    if (!hAdvapi || !hCrypt) return "";

    pfnCredReadW          fnCredReadW = (pfnCredReadW)GetProcAddress(hAdvapi, "CredReadW");
    pfnCredFree           fnCredFree = (pfnCredFree)GetProcAddress(hAdvapi, "CredFree");
    pfnCryptStringToBinaryW fnCryptStringToBinaryW = (pfnCryptStringToBinaryW)GetProcAddress(hCrypt, "CryptStringToBinaryW");
    pfnCryptUnprotectData fnCryptUnprotectData = (pfnCryptUnprotectData)GetProcAddress(hCrypt, "CryptUnprotectData");

    MY_CREDENTIALW* pCred = nullptr;

    //std::cout << "[*] 正在读取凭据: " << targetName << "\n";
    if (!fnCredReadW(sTarget.c_str(), 1, 0, &pCred) || !pCred || !pCred->CredentialBlob) {
        std::cout << "[-] CredReadW 读取失败，请检查凭据名称。\n";
        FreeLibrary(hAdvapi); FreeLibrary(hCrypt);
        return "";
    }
    //std::cout << "[+] 读取成功! 大小: " << pCred->CredentialBlobSize << " 字节\n";

    size_t charCount = pCred->CredentialBlobSize / sizeof(wchar_t);
    std::wstring base64WStr(reinterpret_cast<wchar_t*>(pCred->CredentialBlob), charCount);
    while (!base64WStr.empty() && (base64WStr.back() == L'\0' || iswspace(base64WStr.back()))) {
        base64WStr.pop_back();
    }

    fnCredFree(pCred);

    DWORD decodedLen = 0;
    if (!fnCryptStringToBinaryW(base64WStr.c_str(), 0, MY_CRYPT_STRING_BASE64, nullptr, &decodedLen, nullptr, nullptr)) {
        std::cout << "[-] CryptStringToBinaryW 解析 Base64 失败! 错误码: " << GetLastError() << "\n";
        FreeLibrary(hAdvapi); FreeLibrary(hCrypt);
        return "";
    }

    std::vector<BYTE> cipherBytes(decodedLen);
    fnCryptStringToBinaryW(base64WStr.c_str(), 0, MY_CRYPT_STRING_BASE64, cipherBytes.data(), &decodedLen, nullptr, nullptr);
    //std::cout << "[+] Base64 解码成功! DPAPI 密文真实大小: " << decodedLen << " 字节\n";

    MY_DATA_BLOB dataIn = { static_cast<DWORD>(cipherBytes.size()), cipherBytes.data() };
    MY_DATA_BLOB entropyBlob = { sizeof(g_QuantumEntropy), const_cast<BYTE*>(g_QuantumEntropy) };
    MY_DATA_BLOB dataOut = { 0, nullptr };

    std::string plainText = "";
    BOOL decryptSuccess = FALSE;

    DWORD flagsList[] = { 0x04, 0x01, 0x00 };
    for (DWORD flag : flagsList) {
        //std::cout << "[*] 尝试使用 Flag [0x0" << flag << "] 解密... ";
        if (fnCryptUnprotectData(&dataIn, nullptr, &entropyBlob, nullptr, nullptr, flag, &dataOut)) {
            std::cout << "成功!\n";
            plainText.assign(reinterpret_cast<char*>(dataOut.pbData), dataOut.cbData);
            SecureZeroMemory(dataOut.pbData, dataOut.cbData);
            LocalFree(dataOut.pbData);
            decryptSuccess = TRUE;
            break;
        }
        else {
            DWORD err = GetLastError();
            std::cout << "失败 (错误码: 0x" << std::hex << err << std::dec << ")\n";
        }
    }

    if (!decryptSuccess) {
        std::cout << "\n[!] 严重警告: DPAPI 拒绝解密该数据。\n";
    }
    else {
        //std::cout << "[+] 最终明文解密成功!\n";
    }

    FreeLibrary(hAdvapi); FreeLibrary(hCrypt);
    //std::cout << "========== [DECRYPT END] ==========\n\n";
    return plainText;
}

// ==========================================
// 主程序入口
// ==========================================
int main() {
    try {
        size_t cnt = 0;
        std::cout << "=== Argon2id Cryptographic Test Program ===" << std::endl;
        std::cout << "Target Configuration: 2048 MiB (2GB) RAM, 4 iterations, 1 thread (AVX2 auto-enabled)" << std::endl;

        std::vector<unsigned char> salt = generate_argon2_salt();
        std::string pass = GetDecryptedSecret_Final(OBFUSCATE_STR("limo").c_str());

        Argon2id argon(pass, salt);
        SecureZeroMemory(pass.data(), pass.size());
        std::string password(Argon2id::to_hex(argon.derive_binary()));

        Argon2id argon2id(
            read_windows_credential_utf8(std::wstring(OBFUSCATE_STR(L"filedle").c_str())),
            (string_to_bytes(OBFUSCATE_STR("\x8F\x3C\xA1\x7E\x5D\x2B\x90\x44\x12\x6E\xF5\x8A\x33\xC9\x7B\xE4")))
        );
        std::string password_long(Argon2id::to_hex(argon2id.derive_binary()));

        std::cout << "Please enter the password for verification: " << std::flush;

        // 密码校验循环
        do {
            Argon2id password_derived(std::string(read_secure_password_utf8()), salt);
            if (secure_compare(Argon2id::to_hex(password_derived.derive_binary()), password)) {
                std::cout << "Password verified successfully!" << std::endl;
                SecureZeroMemory(password.data(), password.size());
                goto ss_main;
            }
            std::cout << "Incorrect password. Please try again: " << std::flush;
            ++cnt;
            size_t wait_time = 15 * cnt;
            while (wait_time > 0) {
                std::cout << "\rPlease wait " << wait_time << " seconds before retrying...     " << std::flush;
                sleep_for_seconds(1);
                --wait_time;
            }
            std::cout << "\rYou can now try again. Please enter the password: " << std::flush;
        } while (true);

    ss_main:
        play_unlock_animation();

        std::clog << "Would you like to encrypt or decrypt? (1 to encrypt, 2 to decrypt): " << std::endl;
        int choice = 0;
        std::cin >> choice;
        // 清理 cin 中的换行符，防止影响后续的 std::getline
        std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');

        if (choice == 2) {
            // =========================================================
            // 解密逻辑
            // =========================================================
            std::string filepart;
            std::cout << "Please enter the file path to decrypt: " << std::flush;
            std::getline(std::cin, filepart);

            // 1. 一次性读取完整加密文件
            std::string file_raw = read_txt_to_string(filepart);
            if (file_raw.size() < 16) {
                throw std::runtime_error("File size is too small or invalid (missing salt prefix)!");
            }

            // 2. 拆分文件头 16 字节 Salt 与后续的 Ciphertext Payload
            std::string salt1 = file_raw.substr(0, 16);
            std::vector<uint8_t> cipher_payload(file_raw.begin() + 16, file_raw.end());

            // 3. 执行解密
            std::string decrypted = CRYPTO::XChaCha20Poly1305::decrypt_to_string(cipher_payload, password_long, salt1);

            std::cout << "\nDecrypted content: " << std::endl;
            std::cout << decrypted << std::endl;

            // 4. 清理敏感数据 (注意：绝对不要清理 filepart)
            SecureZeroMemory(cipher_payload.data(), cipher_payload.size());
            SecureZeroMemory(decrypted.data(), decrypted.size());
            SecureZeroMemory(password_long.data(), password_long.size());

        }
        else {
            // =========================================================
            // 加密逻辑
            // =========================================================
            std::string filepart;
            std::cout << "Please enter the file path to save: " << std::flush;
            std::getline(std::cin, filepart);

            std::string file_content;
            std::cout << "Please enter the content to encrypt: " << std::flush;
            std::getline(std::cin, file_content);

            // 1. 生成 16 字节 Salt
            auto salt1 = generate_argon2_salt();
            std::string salt1_str = vector_to_string(salt1);

            // 2. 加密得到密文数组 (Nonce + Ciphertext + Poly1305 Tag)
            std::vector<uint8_t> encrypted = CRYPTO::XChaCha20Poly1305::encrypt(file_content, password_long, salt1_str);

            // 3. 拼装数据结构: [ 16 Bytes Salt ] + [ Encrypted Payload ]
            std::vector<uint8_t> final_file_data;
            final_file_data.reserve(salt1.size() + encrypted.size());
            final_file_data.insert(final_file_data.end(), salt1.begin(), salt1.end());
            final_file_data.insert(final_file_data.end(), encrypted.begin(), encrypted.end());

            // 4. 写入文件
            if (!write_vector_to_file(filepart, final_file_data)) {
                std::cerr << "Error writing encrypted data to file!" << std::endl;
                return -1;
            }

            // 5. 安全擦除内存痕迹
            SecureZeroMemory(salt1.data(), salt1.size());
            SecureZeroMemory(file_content.data(), file_content.size());
            SecureZeroMemory(password_long.data(), password_long.size());

            std::clog << "Encryption completed successfully." << std::endl;
        }

        std::cout << "Press Enter to exit..." << std::flush;
        std::cin.get();

    }
    catch (const std::exception& e) {
        std::cerr << "\n[Error] Exception caught: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}