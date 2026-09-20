#include "activity_collector.h"
#include "http_client.h"
#include "metric_buffer.h"

#include <nlohmann/json.hpp>
#include <curl/curl.h>

//#define NOMINMAX
#include <Windows.h>

#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <vector>
#include <chrono>

using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {
    const size_t MAX_BUFFER = 100;
    const size_t FLUSH_THRESHOLD = 10;
    const auto COLLECT_INTERVAL = 5s;
    const auto FLUSH_INTERVAL = 30s;
    const auto MIN_RETRY = 2s;
    const auto MAX_RETRY = 30s;

    std::atomic<bool> g_stop{false};

    // Обработчик сигналов 
    void signalHandler(int) {
        g_stop.store(true, std::memory_order_relaxed);
    }

    // Обработчик событий консоли
    BOOL WINAPI consoleHandler(DWORD event) {
        if (event == CTRL_C_EVENT || event == CTRL_CLOSE_EVENT || 
            event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT) {
            g_stop.store(true, std::memory_order_relaxed);
            return TRUE;
        }
        return FALSE;
    }

    std::string getAgentId() {
        char name[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD size = sizeof(name);
        if (GetComputerNameA(name, &size)) {
            return std::string(name, size);
        }
        return "UNKNOWN-PC";
    }

    int64_t getUnixTimestamp() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Собираем JSON-пакет для отправки
    std::string buildPacket(const std::string& agent_id, const std::vector<ActivityMetric>& metrics) {
        json packet;
        packet["agent_id"] = agent_id;
        packet["timestamp"] = getUnixTimestamp();
        packet["payload"] = metrics;
        return packet.dump(2);
    }

    // Сохраняем записи в backup.json, чтобы не потерять при остановке
    bool saveBackup(const std::string& agent_id, const std::vector<ActivityMetric>& metrics) {
        if (metrics.empty()) return true;
        try {
            std::string tmp_file = "backup.json.tmp";
            std::string bak_file = "backup.json";
            
            std::ofstream out(tmp_file, std::ios::binary | std::ios::trunc);
            if (!out) return false;

            json bak;
            bak["agent_id"] = agent_id;
            bak["timestamp"] = getUnixTimestamp();
            bak["payload"] = metrics;
            out << bak.dump(2);
            out.close();

            std::error_code ec;
            std::filesystem::remove(bak_file, ec);
            std::filesystem::rename(tmp_file, bak_file, ec);
            return !ec;
        } catch (...) {
            return false;
        }
    }

    // Читаем backup.json при старте
    std::vector<ActivityMetric> loadBackup() {
        if (!std::filesystem::exists("backup.json")) return {};
        try {
            std::ifstream in("backup.json", std::ios::binary);
            json bak;
            in >> bak;
            
            if (!bak.contains("payload") || !bak["payload"].is_array()) return {};
            
            auto metrics = bak["payload"].get<std::vector<ActivityMetric>>();
            if (metrics.size() > MAX_BUFFER) {
                metrics.resize(MAX_BUFFER);
            }
            
            std::filesystem::remove("backup.json");
            return metrics;
        } catch (...) {
            return {};
        }
    }

    void collectorLoop(MetricBuffer& buffer) {
        ActivityCollector collector;
        while (!g_stop.load(std::memory_order_relaxed)) {
            auto m = collector.collect();
            
            if (!buffer.push(m)) {
                std::cerr << "[WARN] Buffer is full, the metric is missing\n";
            } else {
                std::cout << "[+] " << m.process_name << " | " << m.window_title 
                          << " | active=" << (m.user_active ? "true" : "false") << "\n";
            }

            for (auto elapsed = 0ms; elapsed < COLLECT_INTERVAL && !g_stop; elapsed += 100ms) {
                std::this_thread::sleep_for(100ms);
            }
        }
    }

    // Поток-отправки. Ждет накопления 10 метрик или 30 секунд, потом шлет на сервер
    void networkLoop(MetricBuffer& buffer, const std::string& agent_id, const std::string& endpoint) {
        HttpClient client(endpoint);
        auto next_flush = std::chrono::steady_clock::now() + FLUSH_INTERVAL;
        auto next_retry = std::chrono::steady_clock::now();
        auto retry_delay = MIN_RETRY;

        while (!g_stop.load(std::memory_order_relaxed)) {
            auto now = std::chrono::steady_clock::now();
            size_t sz = buffer.size();

            if (sz == 0) {
                buffer.waitFor(1s, [] { return g_stop.load(); });
                continue;
            }

            // Если была ошибка, ждем 
            if (now < next_retry) {
                auto wait = (next_retry - now < 1s) ? (next_retry - now) : 1s;
                buffer.waitFor(std::chrono::duration_cast<std::chrono::milliseconds>(wait), [] { return g_stop.load(); });
                continue;
            }

            bool threshold = sz >= FLUSH_THRESHOLD;
            bool time_is_up = now >= next_flush;

            if (!threshold && !time_is_up) {
                auto wait = (next_flush - now < 1s) ? (next_flush - now) : 1s;
                buffer.waitFor(std::chrono::duration_cast<std::chrono::milliseconds>(wait), [] { return g_stop.load(); });
                continue;
            }

            auto batch = buffer.snapshot(MAX_BUFFER);
            if (batch.empty()) continue;

            std::string body = buildPacket(agent_id, batch);
            long status = 0;
            std::string err;

            std::cout << "[NET] Send " << batch.size() << " metric...\n";
            if (client.postJson(body, status, err)) {
                buffer.erasePrefix(batch.size());
                std::cout << "[NET] Success, HTTP " << status << "\n";
                
                retry_delay = MIN_RETRY;
                next_retry = now;
                next_flush = now + FLUSH_INTERVAL;
            } else {
                std::cerr << "[ERR] Sending error: " << err << ". Retry on " << retry_delay.count() << "s\n";
                next_retry = now + retry_delay;
                retry_delay = std::min(retry_delay * 2, MAX_RETRY);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::string endpoint = "http://127.0.0.1:8080/metrics";
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--url" && i + 1 < argc) {
            endpoint = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: insider [--url URL]\n";
            return 0;
        }
    }

    // Добавляем обработчики сигналов для завершения работы
    std::signal(SIGINT, signalHandler);
    #ifdef SIGTERM
    std::signal(SIGTERM, signalHandler);
    #endif
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    std::string agent_id = getAgentId();
    MetricBuffer buffer(MAX_BUFFER);

    // Подгружаем бэкап с прошлого запуска, если он есть
    auto restored = loadBackup();
    for (const auto& m : restored) {
        if (!buffer.push(m)) break;
    }
    if (!restored.empty()) {
        std::cout << "[INFO] Restored " << restored.size() << " metrics from backup.json\n";
    }

    std::cout << "=== Insider Agent Started ===\n";
    std::cout << "Agent ID: " << agent_id << "\n";
    std::cout << "Endpoint: " << endpoint << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::cerr << "[FATAL] curl_global_init() failed\n";
        return 1;
    }

    std::thread t_collector(collectorLoop, std::ref(buffer));
    std::thread t_network(networkLoop, std::ref(buffer), std::cref(agent_id), std::cref(endpoint));

    // Ждем сигнала остановки
    while (!g_stop.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(200ms);
    }

    std::cout << "\n[INFO] Shutdown requested...\n";
    buffer.notifyAll();
    t_collector.join();
    t_network.join();

    // Сохраняем то, что не успели отправить
    auto unsent = buffer.snapshot(MAX_BUFFER);
    if (!unsent.empty()) {
        if (saveBackup(agent_id, unsent)) {
            std::cout << "[INFO] Save " << unsent.size() << " metric in backup.json\n";
        }
    } else {
        std::error_code ec;
        std::filesystem::remove("backup.json", ec);
    }

    std::cout << "[STAT] The metric was missed due to a buffer overflow: " << buffer.droppedCount() << "\n";
    
    curl_global_cleanup();
    SetConsoleCtrlHandler(consoleHandler, FALSE);
    return 0;
}