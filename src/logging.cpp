/**
 * @file logging.cpp
 * @brief Implementation of Muninn logging system
 */

#include "muninn/logging.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_portable(path) _mkdir(path)
#else
#include <sys/stat.h>
#define mkdir_portable(path) mkdir(path, 0755)
#endif

namespace muninn {

// Static member initialization
LogCallback Logger::callback_ = nullptr;
std::ofstream Logger::log_file_;
std::mutex Logger::file_mutex_;
bool Logger::file_logging_enabled_ = false;
std::string Logger::log_directory_;

void Logger::set_callback(LogCallback callback) {
    callback_ = callback;
}

LogCallback Logger::get_callback() {
    return callback_;
}

bool Logger::set_log_directory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(file_mutex_);

    // Close existing file if open
    if (log_file_.is_open()) {
        log_file_.close();
    }

    // Create directory if it doesn't exist (best effort)
    mkdir_portable(directory.c_str());

    // Build log file path
    std::string log_path = directory;
    if (!log_path.empty() && log_path.back() != '/' && log_path.back() != '\\') {
        log_path += "/";
    }
    log_path += "muninn.log";

    // Open file in append mode
    log_file_.open(log_path, std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "[Muninn] Failed to open log file: " << log_path << "\n";
        file_logging_enabled_ = false;
        return false;
    }

    log_directory_ = directory;
    file_logging_enabled_ = true;

    // Write session header
    log_file_ << "\n";
    log_file_ << "═══════════════════════════════════════════════════════════\n";
    log_file_ << "  Muninn Transcription Engine - Session Started\n";
    log_file_ << "  " << get_timestamp() << "\n";
    log_file_ << "═══════════════════════════════════════════════════════════\n";
    log_file_.flush();

    return true;
}

void Logger::disable_file_logging() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (log_file_.is_open()) {
        log_file_ << "[" << get_timestamp() << "] Session ended\n";
        log_file_.close();
    }
    file_logging_enabled_ = false;
    log_directory_.clear();
}

bool Logger::is_file_logging_enabled() {
    return file_logging_enabled_;
}

void Logger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warn(const std::string& message) {
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    // Always write to file if enabled
    if (file_logging_enabled_) {
        write_to_file(level, message);
    }

    // Use callback if set, otherwise default to stdout/stderr
    if (callback_) {
        callback_(level, message);
    } else {
        // Default to stdout/stderr
        const char* prefix = "[Muninn] ";
        switch (level) {
            case LogLevel::Debug:
                std::cout << prefix << "[DEBUG] " << message << "\n";
                break;
            case LogLevel::Info:
                std::cout << prefix << message << "\n";
                break;
            case LogLevel::Warning:
                std::cerr << prefix << "[WARN] " << message << "\n";
                break;
            case LogLevel::Error:
                std::cerr << prefix << "[ERROR] " << message << "\n";
                break;
        }
        std::cout.flush();
        std::cerr.flush();
    }
}

void Logger::write_to_file(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (!log_file_.is_open()) return;

    log_file_ << "[" << get_timestamp() << "] "
              << "[" << level_to_string(level) << "] "
              << message << "\n";
    log_file_.flush();
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "???";
    }
}

} // namespace muninn
