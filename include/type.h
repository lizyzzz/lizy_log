#ifndef TYPE_H_
#define TYPE_H_

#include <stdint.h>

using int64 = int64_t;
using uint64 = uint64_t;
using int32 = int32_t;
using uint32 = uint32_t;
using LogSeverity = int;
using WallTime = double;

const int LOG_INFO = 0, LOG_WARNING = 1, LOG_ERROR = 2, LOG_FATAL = 3, NUM_SEVERITIES = 4;

enum LogColor {
  COLOR_DEFAULT,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_YELLOW
};

enum PRIVATE_Counter {COUNTER};

enum { PATH_SEPARATOR = '/'};

#endif