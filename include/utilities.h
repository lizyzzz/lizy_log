#ifndef UTILITIES_H_
#define UTILITIES_H_
#include "type.h"
#include <sys/time.h>
#include <cstring>

bool IsGoogleLoggingInitialized();

namespace glog_internal_namespace_ {

// 返回自起始时间的微秒数(int64)
int64 CycleClock_Now();
// 返回字起始时间的微秒数(double)
WallTime WallTime_Now();

// 获取路径在'/'的最后一个名字
const char* const_basename(const char* filepath);

struct CrashReason {
  CrashReason() = default;

  const char* filename{nullptr};
  int line_number{0};
  const char* message{nullptr};

  // 用于保存错误的栈信息
  void* stack[32];
  int depth{0};
};

void SetCrashReason(const CrashReason* r);

void InitGoogleLoggingUtilities(const char* argv0);
void ShutdownGoogleLoggingUtilities();

}



#endif