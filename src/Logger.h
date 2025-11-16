#pragma once

#include <fstream>
#include <string>
#include <string_view>
#include <filesystem>
#include <chrono>

class Logger {
    
public:
    explicit Logger(const std::string& logFolder_, const std::string& logName);
    void write(std::string_view message);

private:
    std::string logFolder_;
    std::string logName_;
    std::uint64_t maxBytes_;
    std::uint16_t maxFiles_;
    std::uint16_t filesCounter_;


    std::ofstream logStream_;
    std::ofstream queryStream_;
    bool check_oversize();
    void rotate();
    void delete_oldest_file();
    std::string make_log_path(uint16_t counter);
    std::string current_timestamp();
};
