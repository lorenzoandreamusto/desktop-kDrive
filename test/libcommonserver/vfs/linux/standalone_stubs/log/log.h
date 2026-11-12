#pragma once
#include <sstream>
#define LOG_ERROR(logger, event) do { std::ostringstream s_; s_ << event; } while (0)
#define LOG_WARN(logger, event) do { std::ostringstream s_; s_ << event; } while (0)
#define LOG_INFO(logger, event) do { std::ostringstream s_; s_ << event; } while (0)
#define LOG_DEBUG(logger, event) do { std::ostringstream s_; s_ << event; } while (0)
#define LOGW_ERROR(logger, event) do { std::wostringstream s_; s_ << event; } while (0)
#define LOGW_WARN(logger, event) do { std::wostringstream s_; s_ << event; } while (0)
#define LOGW_INFO(logger, event) do { std::wostringstream s_; s_ << event; } while (0)
#define LOGW_DEBUG(logger, event) do { std::wostringstream s_; s_ << event; } while (0)
