#include "utilities.h"


namespace glog_internal_namespace_ {

int64 CycleClock_Now() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  return static_cast<int64>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

WallTime WallTime_Now() {
  return CycleClock_Now() * 0.000001;
}

const char* const_basename(const char* filepath) {
  const char* base = strrchr(filepath, '/');
  return base ? (base + 1) : filepath;
}

}