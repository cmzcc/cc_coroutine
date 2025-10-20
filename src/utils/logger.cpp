#include "utils/logger.h"
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>
#include <unordered_map>
#include <mutex>

namespace modern_coro
{
    namespace logger
    {

        Logger &Logger::instance()
        {
            static Logger instance;
            return instance;
        }

        void Logger::init(
            const std::string &log_file,
            LogLevel level,
            size_t max_file_size,
            size_t max_files)
        {
            if (initialized_)
            {
                return;
            }

            try
            {
                // 确保日志目录存在
                std::filesystem::path log_path(log_file);
                if (log_path.has_parent_path())
                {
                    std::filesystem::create_directories(log_path.parent_path());
                }

                // 创建多个 sink
                std::vector<spdlog::sink_ptr> sinks;

                // 控制台 sink（带颜色）
                auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console_sink->set_level(spdlog::level::trace);
                sinks.push_back(console_sink);

                // 文件 sink（循环日志）
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    log_file, max_file_size, max_files);
                file_sink->set_level(spdlog::level::trace);
                sinks.push_back(file_sink);

                // 创建异步 logger
                spdlog::init_thread_pool(8192, 1); // 队列大小 8192，1个后台线程
                default_logger_ = std::make_shared<spdlog::async_logger>(
                    "modern_coro",
                    sinks.begin(),
                    sinks.end(),
                    spdlog::thread_pool(),
                    spdlog::async_overflow_policy::block);

                // 设置日志格式：[时间] [线程ID] [级别] [logger名] 消息
                default_logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] [%n] %v");

                // 设置日志级别
                set_level(level);

                // 注册为默认 logger
                spdlog::set_default_logger(default_logger_);

                // 设置刷新策略：error 级别自动刷新
                default_logger_->flush_on(spdlog::level::err);

                initialized_ = true;

                LOG_INFO("Logger initialized: file={}, level={}, max_size={}, max_files={}",
                         log_file, static_cast<int>(level), max_file_size, max_files);
            }
            catch (const spdlog::spdlog_ex &ex)
            {
                std::cerr << "Logger initialization failed: " << ex.what() << std::endl;

                // 回退到简单的控制台 logger
                default_logger_ = spdlog::stdout_color_mt("modern_coro");
                default_logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%^%l%$] %v");
                initialized_ = true;
            }
        }

        void Logger::set_level(LogLevel level)
        {
            if (!default_logger_)
            {
                // 如果还未初始化，先初始化
                init();
            }

            spdlog::level::level_enum spdlog_level;
            switch (level)
            {
            case LogLevel::TRACE:
                spdlog_level = spdlog::level::trace;
                break;
            case LogLevel::DEBUG:
                spdlog_level = spdlog::level::debug;
                break;
            case LogLevel::INFO:
                spdlog_level = spdlog::level::info;
                break;
            case LogLevel::WARN:
                spdlog_level = spdlog::level::warn;
                break;
            case LogLevel::ERROR:
                spdlog_level = spdlog::level::err;
                break;
            case LogLevel::CRITICAL:
                spdlog_level = spdlog::level::critical;
                break;
            case LogLevel::OFF:
                spdlog_level = spdlog::level::off;
                break;
            default:
                spdlog_level = spdlog::level::info;
                break;
            }

            default_logger_->set_level(spdlog_level);
        }

        std::shared_ptr<spdlog::logger> Logger::get_module_logger(const std::string &module_name)
        {
            static std::mutex mutex;
            static std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> module_loggers;

            std::lock_guard<std::mutex> lock(mutex);

            // 检查是否已存在
            auto it = module_loggers.find(module_name);
            if (it != module_loggers.end())
            {
                return it->second;
            }

            // 创建新的模块 logger（使用默认 logger 的 sinks）
            if (!default_logger_)
            {
                init();
            }

            auto module_logger = default_logger_->clone(module_name);
            module_loggers[module_name] = module_logger;

            return module_logger;
        }

        void Logger::flush()
        {
            if (default_logger_)
            {
                default_logger_->flush();
            }
        }

        Logger::~Logger()
        {
            if (default_logger_)
            {
                default_logger_->flush();
            }
            spdlog::shutdown();
        }

    } // namespace logger
} // namespace modern_coro
