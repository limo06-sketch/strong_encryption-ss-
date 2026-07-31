#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <sstream>
#include <fstream>
#include "Argon2id.h"
#include "toolkit.h"
#include "ObfuscatedString.h"
#include "XCHACHA20_POLY1305.h"

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

    // 旋转字符集（模拟锁芯/钥匙旋转）
    const char rot_chars[] = { '-', '/', '|', '\\' };

    // ================= 阶段 1：钥匙插锁与 180 度机械转动特效 =================
    for (int angle = 0; angle <= 180; angle += 15) {
        std::system("cls");

        char cur_rot = rot_chars[(angle / 15) % 4];
        int percent = (angle * 100) / 180;

        std::cout << "\033[1;33m[!] CRACKING: ENGAGING MECHANICAL KEY...\033[0m\n";
        std::cout << "\033[1;30m[!] TURNING TUMBLER: " << angle << "° / 180° [" << cur_rot << "]\033[0m\n\n";

        // 锁体 ANSI 绘制（中间带转动中的锁芯标识）
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

        // 动态转动进度条
        std::cout << "\033[1;33m[ROTATING] [";
        int bar_width = 20;
        int pos = (percent * bar_width) / 100;
        for (int p = 0; p < bar_width; ++p) {
            if (p < pos) std::cout << "=";
            else if (p == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << percent << "%\033[0m\n" << std::flush;

        // 步进控制：让转动过程平滑且有节奏感
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    // ================= 阶段 2：锁扣机械解除与摆动脱钩 =================
    const char* swing_frames[] = {
        // 帧 A：锁舌脱离（左侧开口）
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

        // 帧 B：锁钩向外旋转偏转
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

    // ================= 阶段 3：终极破译成功（1:1 复刻图片效果） =================
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

/**
 * @brief 读取文本文件内容到 string 中（完整保留格式和换行符）
 * @param filepath 文件路径
 * @return std::string 文件内容
 */
static std::string read_txt_to_string(const std::string& filepath) {
    // 以二进制模式打开文件，避免 Windows/Linux 换行符（\r\n vs \n）被自动转换
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath);
    }

    // 利用流迭代器直接将整个文件读入 string
    return std::string(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );
}

static std::string vector_to_string(const std::vector<unsigned char>& vec) {
    if (vec.empty()) {
        return std::string();
    }
    // 使用 reinterpret_cast 将 unsigned char* 转换为 const char*
    return std::string(
        reinterpret_cast<const char*>(vec.data()),
        vec.size()
    );
}

/**
 * @brief 将 std::vector<uint8_t> 写入到二进制文件
 * @param filepath 目标文件路径
 * @param data 要写入的数据向量
 * @return bool 是否写入成功
 */
static bool write_vector_to_file(const std::string& filepath, const std::vector<uint8_t>& data) {
    // 以二进制模式和覆盖写入模式打开文件
    std::ofstream file(filepath, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // 如果 vector 为空，直接关闭并返回成功
    if (data.empty()) {
        return true;
    }

    // 核心写入代码：传入底层指针和总字节数
    file.write(reinterpret_cast<const char*>(data.data()), data.size());

    // 检查写入过程中是否出错
    if (!file) {
        return false;
    }

    file.close();
    return true;
}

inline static std::vector<uint8_t> string_to_bytes(const std::string& str) {
    return std::vector<uint8_t>(str.begin(), str.end());
}

static void sleep_for_seconds(size_t seconds) {
    precise_busy_wait_dual_core(seconds*1000*1000,2);
}

int main() {
    try {
        std::string salt1 = {"\x8F\x3C\xA1\x7E\x5D\x2B\x90\x44\x12\x6E\xF5\x8A\x33\xC9\x7B\xE4"};
		size_t cnt = 0;
        std::cout << "=== Argon2id Cryptographic Test Program ===" << std::endl;
        std::cout << "Target Configuration: 2048 MiB (2GB) RAM, 4 iterations, 1 thread (AVX2 auto-enabled)" << std::endl;
        std::vector<unsigned char> salt = generate_argon2_salt();
        Argon2id argon(read_windows_credential_utf8(std::wstring(OBFUSCATE_STR(L"limo"))), salt);
        std::string password(Argon2id::to_hex(argon.derive_binary()));
        Argon2id argon2id(read_windows_credential_utf8(std::wstring(OBFUSCATE_STR(L"filedle"))), (string_to_bytes(salt1)));
        std::string password_long(Argon2id::to_hex(argon2id.derive_binary()));

        std::cout << "Please enter the password for verification: " << std::flush;

        do {
            Argon2id password_derived(std::string(read_secure_password_utf8()), salt);
            if(secure_compare(Argon2id::to_hex(password_derived.derive_binary()),password))
            {
                std::cout << "Password verified successfully!" << std::endl;
                SecureZeroMemory(password.data(), password .size());
				goto ss_main;
            }
			std::cout << "Incorrect password. Please try again: " << std::flush;
            ++cnt;
            size_t time = 15 * cnt;
            while(time>0)
            {
				std::cout << "\rPlease wait " << time << " seconds before retrying...     " << std::flush;
                sleep_for_seconds(1);
                --time;
            }
			std::cout << "\rYou can now try again. Please enter the password: " << std::flush;
        } while (true);
    ss_main:
        play_unlock_animation();
		std::clog << "Would you like to encrypt or decrypt?(1 to encrypt, 2 to decrypt): " << std::endl;
		bool encrypt = false;
		int choice;
		std::cin >> choice;
        if(choice == 2){
            encrypt = true;
        }
        if(encrypt){
            std::string filepart;
			std::cout << "Please enter the file path (e.g.): " << std::flush;
            std::cin.clear();
            std::cin.ignore();
            getline(std::cin, filepart);
            std::string file_content = read_txt_to_string(filepart);
            SecureZeroMemory(filepart.data(), filepart.size());
            std::vector<unsigned char> vec(file_content.begin(), file_content.end());
            std::string decrypted = CRYPTO::XChaCha20Poly1305::decrypt_to_string(vec, password_long, salt1);
			std::cout << "Decrypted content: " << std::endl;
			std::cout << decrypted << std::endl;
            SecureZeroMemory(vec.data(), vec.size());
            SecureZeroMemory(decrypted.data(), decrypted.size());
            SecureZeroMemory(password_long.data(), password_long.size());
        }
        else {
            std::string filepart;
            std::cout << "Please enter the file path (e.g.): " << std::flush;
            std::cin.clear();
            std::cin.ignore();
            getline(std::cin, filepart);
            std::string file_content = std::string();
            std::cout<<"Please enter the content to encrypt: " << std::flush;
            std::cin.clear();
            std::cin.ignore();
            getline(std::cin, file_content);
            std::vector<uint8_t> encrypted = CRYPTO::XChaCha20Poly1305::encrypt(file_content, password_long,salt1);
            if (!write_vector_to_file(filepart, encrypted)) {
				std::cerr << "error" << std::endl;
                return -1;
            }
            SecureZeroMemory(filepart.data(), filepart.size());
            SecureZeroMemory(file_content.data(), file_content.size());
            SecureZeroMemory(password_long.data(), password_long.size());
            std::clog << "Encrypted is ok..." << std::endl;
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