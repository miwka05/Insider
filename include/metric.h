#pragma once
#include <string>
#include <nlohmann/json.hpp>

// Структура одной записи
struct ActivityMetric {
    std::string time; //время
    std::string process_name; //имя процесса
    std::string window_title; //заголовок окна
    bool user_active = false; //была ли активность в последнее время
};

// Сериализация в JSON
inline void to_json(nlohmann::json& j, const ActivityMetric& m) {
    j = nlohmann::json{
        {"time", m.time},
        {"process_name", m.process_name},
        {"window_title", m.window_title},
        {"user_active", m.user_active}
    };
}

// Десериализация JSON
inline void from_json(const nlohmann::json& j, ActivityMetric& m) {
    j.at("time").get_to(m.time);
    j.at("process_name").get_to(m.process_name);
    j.at("window_title").get_to(m.window_title);
    j.at("user_active").get_to(m.user_active);
}
