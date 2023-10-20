#include "logging.h"


// 宏定义
#define COMPACT_LIZY_LOG_INFO LogMessage(__FILE__, __LINE__)

#define COMPACT_LIZY_LOG_WARNING LogMessage(__FILE__, __LINE__, GLOG_WARNING)

#define COMPACT_LIZY_LOG_ERROR LogMessage(__FILE__, __LINE__, GLOG_ERROR)

#define COMPACT_LIZY_LOG_FATAL LogMessage(__FILE__, __LINE__, GLOG_FATAL)

#define LOG(severity) COMPACT_LIZY_LOG_ ## severity.stream()




