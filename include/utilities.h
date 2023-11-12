#ifndef LIZY_UTILITIES_H_
#define LIZY_UTILITIES_H_
#pragma once
#include "type.h"
#include <sys/time.h>
#include <unistd.h>
#include <string>
#include <cstring>


namespace log_internal_namespace_ {

// 返回自起始时间的微秒数(int64)
int64 CycleClock_Now();
// 返回自起始时间的微秒数(double)
WallTime WallTime_Now();

int64 UsecToCycles(int64 usec);

// 获取路径在'/'的最后一个名字
const char* const_basename(const char* filepath);

template<class T>
inline T sync_val_compare_and_swap(T* ptr, T oldval, T newval) {
  // CAS
  // 如果 *ptr 的值等于 oldval, 则将 newval 存储到 *ptr 中, 并返回 *ptr 原来的值
  // 如果 *ptr 的值不等于 oldval, 则什么都不做, 并返回 *ptr 原来的值
  T ret = *ptr;
  if (ret == oldval) {
    *ptr = newval;
  }
  return ret;
}



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

void InitLoggingUtilities(const char* argv0);
void ShutdownLoggingUtilities();

// 获取程序短名称
const char* ProgramInvocationShortName();
// 进程号是否改变
bool PidHasChanged();

int32 GetMainThreadPid();

// 获取用户名
const std::string& MyUserName();

}



#endif