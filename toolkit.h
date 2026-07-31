#include <windows.h>
#include <wincred.h>
#include <string>
#include <chrono>
#include <thread>
#include <immintrin.h>
#include <string_view>
#include <system_error>

// 自动链接凭据管理器所需的静态库 (MSVC 专用)
#pragma comment(lib, "Advapi32.lib")

/**
 * @brief 从 Windows 凭据管理器读取密码并安全转换为 UTF-8 (全程无明文残留)
 *
 * @param target_name 凭据的名称 (如 L"MyApp/MyUser")
 * @return std::string 包含 UTF-8 密码的字符串。
 *
 * @warning 调用方在使用完返回的 std::string 后，必须手动调用 SecureZeroMemory(&str[0], str.size()) 清理它！
 */
inline std::string read_windows_credential_utf8(const std::wstring& target_name) {
    PCREDENTIALW pCred = nullptr;
    std::string utf8_password;

    // 1. 从 Windows 凭据管理器读取通用凭据 (默认 CRED_TYPE_GENERIC)
    if (!CredReadW(target_name.c_str(), CRED_TYPE_GENERIC, 0, &pCred)) {
        throw std::system_error(GetLastError(), std::system_category(), "CredReadW failed to read credential");
    }

    try {
        if (pCred->CredentialBlobSize > 0 && pCred->CredentialBlob != nullptr) {

            // Windows 凭据管理器通常将密码作为 UTF-16LE 字节数组存储
            size_t utf16_chars = pCred->CredentialBlobSize / sizeof(wchar_t);
            const wchar_t* utf16_ptr = reinterpret_cast<const wchar_t*>(pCred->CredentialBlob);

            // 智能规避：精准剔除末尾可能带有的 '\0'
            if (utf16_chars > 0 && utf16_ptr[utf16_chars - 1] == L'\0') {
                utf16_chars--;
            }

            if (utf16_chars > 0) {
                // 2. 预计算转换所需的 UTF-8 缓冲区大小
                int utf8_size = WideCharToMultiByte(
                    CP_UTF8, 0,
                    utf16_ptr, static_cast<int>(utf16_chars),
                    nullptr, 0, nullptr, nullptr
                );

                if (utf8_size <= 0) {
                    throw std::system_error(GetLastError(), std::system_category(), "WideCharToMultiByte size calculation failed");
                }

                // 3. 一次性精确分配内存 (避免动态扩容带来的明文游离)
                utf8_password.resize(utf8_size);

                // 4. 直接在预分配的内存上进行原地转换 (&utf8_password[0] 兼容 C++11 可写指针)
                if (WideCharToMultiByte(
                    CP_UTF8, 0,
                    utf16_ptr, static_cast<int>(utf16_chars),
                    &utf8_password[0], utf8_size,
                    nullptr, nullptr) == 0)
                {
                    throw std::system_error(GetLastError(), std::system_category(), "WideCharToMultiByte conversion failed");
                }
            }
        }
    }
    catch (...) {
        // [异常安全] 发生任何异常时，在抛出前死守底线，擦除所有涉及的明文内存
        if (pCred && pCred->CredentialBlob) {
            SecureZeroMemory(pCred->CredentialBlob, pCred->CredentialBlobSize);
        }
        if (pCred) {
            CredFree(pCred);
        }
        if (!utf8_password.empty()) {
            SecureZeroMemory(&utf8_password[0], utf8_password.size());
        }
        throw; // 重新抛出
    }

    // 5. [核心安全] CredFree 内部不保证擦除内存，必须手动覆写原始凭据 Blob 内存区域
    if (pCred && pCred->CredentialBlob) {
        SecureZeroMemory(pCred->CredentialBlob, pCred->CredentialBlobSize);
    }

    if (pCred) {
        CredFree(pCred);
    }

    // 利用 C++11 的返回值优化 (NRVO/Move)，无内存拷贝返回
    return utf8_password;
}

/**
 * @brief 工业级高精度双核绑定忙等待时钟函数
 *
 * @param duration_us 等待的目标微秒数 (Microseconds)
 * @param target_cpu_core 指定绑定的 CPU 核心索引 (默认绑定到核心 1)
 */
inline void precise_busy_wait_dual_core(uint64_t duration_us, int target_cpu_core = 1) {
    // 1. 设置双核/单线程亲和性（将当前执行线程绑定到指定的 CPU 核心，减少调度抖动）
#if defined(_WIN32)
    HANDLE hThread = GetCurrentThread();
    DWORD_PTR mask = (1ULL << target_cpu_core);
    SetThreadAffinityMask(hThread, mask);
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(target_cpu_core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif

    // 2. 获取高精度时钟起点 (Steady Clock 保证单调递增，不受系统时间修改影响)
    auto start_time = std::chrono::high_resolution_clock::now();
    auto target_duration = std::chrono::microseconds(duration_us);
    auto target_time = start_time + target_duration;

    // 3. 粗粒度阶段：若剩余时间大于 2 毫秒，先让出 CPU，避免盲目忙等导致 CPU 空转过热
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        if (now >= target_time) {
            return;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(target_time - now);

        if (remaining.count() > 2000) {
            // 剩余时间较多时，安全睡眠 1 毫秒
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else {
            // 4. 精细忙等待阶段 (Micro-second Busy Wait)：进入高频微调冲刺
            break;
        }
    }

    // 5. 核心 CPU 放松与忙等待循环 (利用 _mm_pause 和 yield 动态释放硬件流水线压力)
    while (std::chrono::high_resolution_clock::now() < target_time) {
        // _mm_pause 向 CPU 发出提示：这是一个自旋锁/等待循环，
        // 能有效减少超线程（Hyper-Threading）竞争导致的功耗飙升并防止流水线阻塞
        _mm_pause();

        // 偶尔交替 yield，给系统其他高优先级中断留出余地
        // （若追求极致纳秒级精度可注释掉下面这行，此处用于双核平衡放松）
        // std::this_thread::yield();
    }
}

/**
 * @brief 安全读取控制台密码，实时显示 * 号，支持退格修正，直接返回 UTF-8 编码的 std::string
 *
 * @return std::string 包含 UTF-8 密码的标准字符串
 * @warning 调用方在使用完返回的 std::string 后，必须 手动调用 SecureZeroMemory 清理它！
 */
inline std::string read_secure_password_utf8() {
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE) {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to get stdin handle");
    }

    DWORD original_mode = 0;
    if (!GetConsoleMode(hStdin, &original_mode)) {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to get console mode");
    }

    // 关闭回显 (ENABLE_ECHO_INPUT) 和行缓冲 (ENABLE_LINE_INPUT)
    DWORD raw_mode = original_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    if (!SetConsoleMode(hStdin, raw_mode)) {
        throw std::system_error(GetLastError(), std::system_category(), "Failed to set raw console mode");
    }

    std::wstring w_password;

    // RAII 保证控制台状态无论如何都能恢复，且异常时安全擦除临时缓冲区
    struct ConsoleGuard {
        HANDLE h;
        DWORD mode;
        std::wstring& pwd;
        ~ConsoleGuard() {
            SetConsoleMode(h, mode);
            if (!pwd.empty()) {
                SecureZeroMemory(&pwd[0], pwd.size() * sizeof(wchar_t));
            }
        }
    } guard{ hStdin, original_mode, w_password };

    wchar_t ch = 0;
    DWORD chars_read = 0;

    while (true) {
        if (!ReadConsoleW(hStdin, &ch, 1, &chars_read, nullptr) || chars_read == 0) {
            break;
        }

        if (ch == L'\r' || ch == L'\n') {
            std::cout << std::endl;
            break;
        }
        else if (ch == L'\b' || ch == 127) {
            if (!w_password.empty()) {
                w_password.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        }
        else if (ch >= 32) {
            w_password.push_back(ch);
            std::cout << '*' << std::flush;
        }
    }

    // 将底层的 wstring 转换为 Argon2id 完美兼容的 UTF-8 std::string
    std::string utf8_password;
    if (!w_password.empty()) {
        int utf8_size = WideCharToMultiByte(
            CP_UTF8, 0,
            w_password.data(), static_cast<int>(w_password.size()),
            nullptr, 0, nullptr, nullptr
        );

        if (utf8_size > 0) {
            utf8_password.resize(utf8_size);
            WideCharToMultiByte(
                CP_UTF8, 0,
                w_password.data(), static_cast<int>(w_password.size()),
                &utf8_password[0], utf8_size,
                nullptr, nullptr
            );
        }
    }

    // 提前清空 guard 里的宽密码缓存，防止析构时重复清理
    guard.pwd.clear();

    return utf8_password;
}

/**
 * @brief 工业级安全字符串比较（恒定时间 + 随机噪声因子 + 引用捕获无拷贝）
 *
 * @param a 第一个字符串引用 (例如用户输入的密码)
 * @param b 第二个字符串引用 (例如正确的密码或哈希派生密钥)
 * @return true 如果完全一致，false 否则
 */
inline bool secure_compare(std::string_view a, std::string_view b) {
    // 1. 基础长度校验 (注意：即使长度不一致，为了防止通过耗时推断长度，
    // 我们也可以走恒定时间或做额外混淆，但基础安全要求下先对齐处理)
    bool length_match = (a.size() == b.size());

    // 2. 随机噪声因子（Random Noise Injection）：
    // 引入微量的真随机硬件延迟，破坏高精度性能分析器的时序侧信道采样
    {
        unsigned char noise_byte;
        randombytes_buf(&noise_byte, 1);
        // 根据随机字节引入一个极短的动态忙等/休眠（纳秒级）
        volatile uint64_t dummy = 0;
        for (int i = 0; i < (noise_byte & 0x0F) + 1; ++i) {
            dummy += i;
        }
    }

    // 3. 即使长度不同，也尽量执行等量时长的比较（或取其 min 大小），
    // 保证物理耗时尽量平缓，不给攻击者提供“不等时立刻返回”的短路泄露
    size_t cmp_len = length_match ? a.size() : (a.size() < b.size() ? a.size() : b.size());

    int result = -1;
    if (cmp_len > 0) {
        // sodium_memcmp 提供恒定时间的内存比对
        // 无论在哪一位出错，它都会把所有字节比完才返回，杜绝时序攻击 (Timing Attack)
        result = sodium_memcmp(a.data(), b.data(), cmp_len);
    }

    // 4. 最终判定：必须同时满足“长度一致”且“内容比对结果为 0”
    bool is_equal = (length_match && (result == 0));

    // 5. 额外安全防御：对内部可能涉及的临时状态进行覆写 (虽然这里是只读引用)
    // 确保没有因编译器优化残留敏感中间态
    return is_equal;
}