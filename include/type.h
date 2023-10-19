#ifndef TYPE_H_
#define TYPE_H_

#include <stdint.h>

using int64 = int64_t;
using int32 = int32_t;
using uint32 = uint32_t;
using LogSeverity = int;
using WallTime = double;

const int GLOG_INFO = 0, GLOG_WARNING = 1, GLOG_ERROR = 2, GLOG_FATAL = 3, NUM_SEVERITIES = 4;

enum GLogColor {
  COLOR_DEFAULT,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_YELLOW
};

enum PRIVATE_Counter {COUNTER};

enum { PATH_SEPARATOR = '/'};

#endif