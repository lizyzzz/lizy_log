#include "utilities.h"

static const char* log_program_invocation_short_name = nullptr;

bool IsLoggingInitialized() {
  return log_program_invocation_short_name != nullptr;
}



namespace log_internal_namespace_ {

int64 CycleClock_Now() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  return static_cast<int64>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

WallTime WallTime_Now() {
  return CycleClock_Now() * 0.000001;
}

int64 UsecToCycles(int64 usec) {
  return usec;
}

const char* const_basename(const char* filepath) {
  const char* base = strrchr(filepath, '/');
  return base ? (base + 1) : filepath;
}

const char* ProgramInvocationShortName() {
  if (log_program_invocation_short_name != nullptr) {
    return log_program_invocation_short_name;
  } else {
    return "UNKNOWN";
  }
}


static int32 g_main_thread_pid = getpid();
int32 GetMainThreadPid() {
  return g_main_thread_pid;
}

bool PidHasChanged() {
  int32 pid = getpid();
  if (g_main_thread_pid == pid) {
    return false;
  }
  g_main_thread_pid = pid;
  return true;
}

static std::string g_my_user_name;
const std::string& MyUserName() {
  if (!g_my_user_name.empty()) {
    return g_my_user_name;
  }
  const char* user = getenv("USER");
  if (user != nullptr) {
    g_my_user_name = user;
  }
  if (g_my_user_name.empty()) {
    g_my_user_name = "invalid-user";
  }
  return g_my_user_name;
}

void InitLoggingUtilities(const char* argv0) {
  if (IsLoggingInitialized()) {
    // TODO: 使用 check 来终止程序
    perror("You called InitGoogleLogging() twice!");
  }
  const char* slash = strrchr(argv0, '/');
  log_program_invocation_short_name = slash ? slash + 1 : argv0;
}

void ShutdownLoggingUtilities() {
  if (!IsLoggingInitialized()) {
    perror("You called ShutdownGoogleLogging() without calling InitGoogleLogging() first!");
  }

  log_program_invocation_short_name = nullptr;
}

// 使用原子操作确保竞态安全性
static const CrashReason* g_reason = nullptr;

void SetCrashReason(const CrashReason* r) {
  sync_val_compare_and_swap(&g_reason, reinterpret_cast<const CrashReason*>(0), r);
}


}