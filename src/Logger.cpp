#include "Logger.h"

Logger::Logger(const std::string& logFolder, const std::string& logName)
    : logFolder_(logFolder)
    , logName_(logName)
    , maxBytes_(4ull * 1024ull * 1024ull)
    , maxFiles_(10)
    , filesCounter_(1) {

    if (maxFiles_ < 1) {
        maxFiles_ = 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(logFolder_, ec);
    if (ec) {
        throw std::runtime_error("Failed to create log directory: " + logFolder_ + " - " + ec.message());
    }

    logStream_.open(make_log_path(filesCounter_), std::ios::out | std::ios::app);
    
    if (!logStream_.is_open()) {
        throw std::runtime_error("Failed to open log");
    }
}

void Logger::write(std::string_view message) {

    if (check_oversize()) {
        rotate();
    }

    logStream_ << "[" << current_timestamp() << "] " << message << '\n';
    logStream_.flush();
}

void Logger::rotate() {
    logStream_.close();
    filesCounter_++;
    delete_oldest_file();
    std::string newPath = make_log_path(filesCounter_);
    logStream_.open(newPath, std::ios::out | std::ios::app);
    if (!logStream_.is_open()) {
        throw std::runtime_error("Failed to open log file: " + newPath);
    }
}

void Logger::delete_oldest_file() {
    //  delete the one file 
    if (maxFiles_ <= 1) {
        std::error_code err;
        std::filesystem::remove(make_log_path(filesCounter_), err);
        return;
    }

    if (filesCounter_ > maxFiles_) {
        std::uint16_t oldestIndex = filesCounter_ - maxFiles_;
        std::error_code err;
        std::filesystem::remove(make_log_path(oldestIndex), err);
    }
}

std::string Logger::current_timestamp() {
    std::time_t time = std::time(nullptr);
    std::tm tm;
    localtime_r(&time, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

bool Logger::check_oversize() {
    std::error_code err;
    std::string path = make_log_path(filesCounter_);
    std::uint64_t size = std::filesystem::file_size(path, err);

    if (err || size < maxBytes_) {
        return false;
    } else {
        return true;
    }
}

std::string Logger::make_log_path(uint16_t counter) {
    if (logFolder_.empty()) {
        return logName_ + "-" + std::to_string(counter) + ".log";
    } else {
        return logFolder_ + "/" + logName_ + "-" + std::to_string(counter) + ".log";
    }
}