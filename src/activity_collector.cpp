#include "activity_collector.h"

//#define NOMINMAX
#include <Windows.h>

#include <string>
#include <ctime>

// Функция для конвертации wchar_t
std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    
    std::string res(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr.data(), (int)wstr.size(), res.data(), size, nullptr, nullptr);
    return res;
}

// Получение текущего локального времени в формате ГГГГ-ММ-ДД ЧЧ:ММ:СС
std::string getCurrentLocalTime() {
    time_t now = time(nullptr);
    tm local_time{};
    localtime_s(&local_time, &now);
    char buf[32]{};
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local_time);
    return buf;
}

// Вытаскиваем имя процесса по PID. Если не получилось возвращается "unknown"
std::string getProcessName(DWORD pid) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return "unknown.exe";

    std::wstring path(32768, L'\0');
    DWORD path_size = (DWORD)path.size();
    BOOL ok = QueryFullProcessImageNameW(hProc, 0, path.data(), &path_size);
    CloseHandle(hProc);

    if (!ok || path_size == 0) return "unknown";

    path.resize(path_size);
    // Оставляем только имя файла
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path = path.substr(slash + 1);
    }

    std::string utf8_name = wideToUtf8(path);
    return utf8_name.empty() ? "unknown" : utf8_name;
}

// Провека, был ли пользователь активен за последние 5 секунд
bool isUserActive() {
    LASTINPUTINFO info{};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info)) return false;
    
    DWORD idle_ms = GetTickCount() - info.dwTime;
    return idle_ms <= 5000;
}

ActivityMetric ActivityCollector::collect() const {
    ActivityMetric m;
    m.time = getCurrentLocalTime();
    m.user_active = isUserActive();

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        m.process_name = "unknown";
        return m;
    }

    // Получение заголовка окна
    wchar_t title[1024]{};
    int title_len = GetWindowTextW(hwnd, title, 1024); //заголовки длиной более 1023 символов обрезаются
    if (title_len > 0) {
        m.window_title = wideToUtf8(std::wstring(title, title_len));
    }

    // Получение PID процесса, которому принадлежит окно
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) {
        m.process_name = getProcessName(pid);
    } else {
        m.process_name = "unknown";
    }

    return m;
}