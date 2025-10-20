#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>

namespace modern_coro
{
    namespace logger
    {

        /**
         * @brief 日志级别枚举
         */
        enum class LogLevel
        {
            TRACE = 0,
            DEBUG = 1,
            INFO = 2,
            WARN = 3,
            ERROR = 4,
            CRITICAL = 5,
            OFF = 6
        };

        /**
         * @brief 日志管理器 - 单例模式
         */
        class Logger
        {
        public:
            static Logger &instance();

            // 初始化日志系统
            void init(
                const std::string &log_file = "logs/modern_coro.log",
                LogLevel level = LogLevel::INFO,
                size_t max_file_size = 10 * 1024 * 1024, // 10MB
                size_t max_files = 3);

            // 设置日志级别
            void set_level(LogLevel level);

            // 获取默认 logger
            std::shared_ptr<spdlog::logger> get_logger() const { return default_logger_; }

            // 获取特定模块的 logger
            std::shared_ptr<spdlog::logger> get_module_logger(const std::string &module_name);

            // 刷新所有日志
            void flush();

        private:
            Logger();
            ~Logger();

            Logger(const Logger &) = delete;
            Logger &operator=(const Logger &) = delete;

            std::shared_ptr<spdlog::logger> default_logger_;
            bool initialized_ = false;
        };

// 便捷的日志宏
#define LOG_TRACE(...) modern_coro::logger::Logger::instance().get_logger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) modern_coro::logger::Logger::instance().get_logger()->debug(__VA_ARGS__)
#define LOG_INFO(...) modern_coro::logger::Logger::instance().get_logger()->info(__VA_ARGS__)
#define LOG_WARN(...) modern_coro::logger::Logger::instance().get_logger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) modern_coro::logger::Logger::instance().get_logger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) modern_coro::logger::Logger::instance().get_logger()->critical(__VA_ARGS__)

// 模块级别日志宏
#define LOG_MODULE_TRACE(module, ...) modern_coro::logger::Logger::instance().get_module_logger(module)->trace(__VA_ARGS__)
#define LOG_MODULE_DEBUG(module, ...) modern_coro::logger::Logger::instance().get_module_logger(module)->debug(__VA_ARGS__)
#define LOG_MODULE_INFO(module, ...) modern_coro::logger::Logger::instance().get_module_logger(module)->info(__VA_ARGS__)
#define LOG_MODULE_WARN(module, ...) modern_coro::logger::Logger::instance().get_module_logger(module)->warn(__VA_ARGS__)
#define LOG_MODULE_ERROR(module, ...) modern_coro::logger::Logger::instance().get_module_logger(module)->error(__VA_ARGS__)
#define LOG_MODULE_CRITICAL(module, ...) modern_coro::logger::Logger::instance().get_module_logger(module)->critical(__VA_ARGS__)

        // 特定模块的日志快捷方式
        namespace modules
        {
            constexpr const char *SCHEDULER = "scheduler";
            constexpr const char *IO_MANAGER = "io_manager";
            constexpr const char *TIMER = "timer";
            constexpr const char *COROUTINE = "coroutine";
            constexpr const char *SAFETY = "safety";
            constexpr const char *MEMORY = "memory";
        }

    } // namespace logger
} // namespace modern_coro
