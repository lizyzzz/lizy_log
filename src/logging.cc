#include "logging.h"

using std::setw;

const size_t LogMessage::kMaxLogMessageLen = 30000;

static uint32 MaxLogSize() {
  return (FLAGS_max_log_size > 0 && FLAGS_max_log_size < 4096 ? FLAGS_max_log_size : 1);
}

static void GetTempDirectories(std::vector<std::string>* list) {
  list->clear();
  const char * candidates[] = {
    // Non-null only during unittest/regtest
    getenv("TEST_TMPDIR"),

    // Explicitly-supplied temp dirs
    getenv("TMPDIR"), getenv("TMP"),

    // If all else fails
    "/tmp",
  };

  for (auto d : candidates) {
    if (!d) continue;
    std::string dstr = d;
    if (dstr[dstr.size() - 1] != '/') {
      dstr += "/";
    }
    list->push_back(dstr);

    struct stat statbuff;
    // 获取目录的状态
    if (!stat(d, &statbuff) && S_ISDIR(statbuff.st_mode)) {
      return;
    }
  }

}

static std::vector<string>* logging_directories_list;
const std::vector<std::string>& GetLoggingDirectories() {
  if (logging_directories_list == nullptr) {
    logging_directories_list = new std::vector<std::string>;
    if (!FLAGS_log_dir.empty()) {
      logging_directories_list->push_back(FLAGS_log_dir);
    } else {
      GetTempDirectories(logging_directories_list);
      logging_directories_list->push_back("./");
    }
  }
  return *logging_directories_list;
}


// mutex
static std::mutex log_mutex;

// 每种优先级被发送的信息的数量
// 静态成员变量
int64 LogMessage::num_messages_[NUM_SEVERITIES] = {0, 0, 0, 0};

// 禁止继续记录日志的标记 (当磁盘满时)
static bool stop_writing = false;

const char* const LogSeverityNames[NUM_SEVERITIES] = {
  "INFO", "WARNING", "ERROR", "FATAL"
};

// Has the user called SetExitOnDFatal(true)?
static bool exit_on_dfatal = true;

const char* GetLogSeverityName(LogSeverity severity) {
  return LogSeverityNames[severity];
}



struct LogMessage::LogMessageData {
  LogMessageData();

  int preserved_errno_;      // preserved errno
  // 缓冲区空间
  char message_text_[kMaxLogMessageLen+1];
  LogStream stream_;
  char severity_;
  int line_;
  void (LogMessage::*send_method_)(); // 在析构函数中调用
  union {  // 最多使用一个
    LogSink* sink_;
    std::vector<std::string>* outvec_;
    std::string* message_;
  };

  size_t num_prefix_chars_;
  size_t num_chars_to_log_;
  size_t num_chars_to_syslog_;
  const char* basename_;  // 调用 LOG 的文件名称
  const char* fullname_;  // 调用 LOG 的文件全称
  bool has_been_flushed_; // 是否已经刷盘
  bool first_fatal_;      // 是否是第一条 fatal msg

 private:
  LogMessageData(const LogMessageData&) = delete;
  LogMessageData& operator=(const LogMessageData&) = delete;
};

/* ------------------------------ LogFileObject ------------------------------------------------ */

base::Logger::~Logger() = default;

// 获取网络主机名
static void GetHostName(string* hostname) {
  struct utsname buf;
  if (uname(&buf) < 0) {
    *(buf.nodename) = '\0';
  }
  *hostname = buf.nodename;
}

namespace {
  std::string PrettyDuration(int secs) {
    std::stringstream result;
    int mins = secs / 60;
    int hours = mins / 60;
    mins %= 60;
    secs %= 60;
    result.fill('0');
    result << hours << ':' << setw(2) << mins << ':' << setw(2) << secs;
    return result.str();
  } 


  // 封装所有文件系统的相关状态
  // 默认的文件方式的日志落地
  class LogFileObject : public base::Logger {
   public:
    LogFileObject(LogSeverity severity, const char* base_filename);
    ~LogFileObject() override;

    // force_flush 表示是否在这里 Flush
    void Write(bool force_flush, time_t timestamp, const char* message, size_t message_len) override;

    // 配置选项
    void SetBasename(const char* basename);
    void SetExtension(const char* ext);
    void SetSymlinkBasename(const char* symlink_basename);

    // 正常刷盘接口
    void Flush() override;

    // 返回系统日志记录器(如 INFO, ERROR Logger )实际的日志文件大小
    uint32 LogSize() override {
      std::lock_guard<std::mutex> lk(lock_);
      return file_length_;
    }

    // 内部的刷盘接口, 暴露此接口是为了 FlushLogFilesUnsafe() 可以在不获取锁的情况下调用这个接口
    // 通常 Flush() 在获取锁后才调用这个接口
    void FlushUnlocked();

   private:
    static const uint32 kRolloverAttemptFrequency = 0x20; // 日志滚动频率(大小)

    std::mutex lock_;
    bool base_filename_selected_;
    std::string base_filename_;
    std::string symlink_basename_;
    std::string filename_extension_;
    FILE* file_{nullptr};            // 目标文件
    LogSeverity severity_;
    uint32 bytes_since_flush_{0};   // 上一次刷盘到现在的字节数
    uint32 dropped_mem_length_{0};  // 丢弃文件流中的字节数
    uint32 file_length_{0};         // 文件字节数
    unsigned int rollover_attempt_; // 日志滚动次数(即另外新建一个新的日志文件)
    int64 next_flush_time_{0};      // 经过多少个周期后进行日志刷盘操作
    WallTime start_time_;

    // 根据文件名和可选参数time_pid_string创建日志文件
    // 要求: 必须持有锁
    bool CreateLogfile(const std::string& time_pid_string);
  };

  // 封装所有日志清理相关状态
  class LogCleaner {
   public:
    LogCleaner();

    // 将 overdue_days(逾期天数) 设置为 0 天会删除所有日志
    void Enable(unsigned int overdue_days);
    void Disable();

    void UpdateCleanUpTime();

    void Run(bool base_filename_selected, const std::string& base_filename, const std::string& filename_extension);

    bool enabled() const { return enabled_; }

   private:
    // 获取过期的日志文件名
    std::vector<std::string> GetOverdueLogNames(std::string log_directory, unsigned int days,
                                                const std::string& base_filename,
                                                const std::string& filename_extension) const;
    // 判断文件名是否是日志文件名的格式
    bool IsLogFromCurrentProject(const std::string& filepath,
                                 const std::string& base_filename,
                                 const std::string& filename_extension) const;
    // 距离上次修改的时间是否到期
    bool IsLogLastModifiedOver(const std::string& filepath, unsigned int days) const;

    bool enabled_{false};
    unsigned int overdue_days_{7};
    int64 next_cleanup_time_{0}; // 清理逾期日志的周期计数
  };

  LogCleaner log_cleaner;
}





/* ------------------------------ LogFileObject end ------------------------------------------------ */


// 终端是否支持不同颜色的输出
static bool TerminalSupportsColor() {
  bool term_supports_color = false;

  const char* const term = getenv("TERM");
  if (term != nullptr && term[0] != '\0') {
    term_supports_color = 
      !strcmp(term, "xterm") ||
      !strcmp(term, "xterm-color") ||
      !strcmp(term, "xterm-256color") ||
      !strcmp(term, "screen-256color") ||
      !strcmp(term, "konsole") ||
      !strcmp(term, "konsole-16color") ||
      !strcmp(term, "konsole-256color") ||
      !strcmp(term, "screen") ||
      !strcmp(term, "linux") ||
      !strcmp(term, "cygwin");
  }
  return term_supports_color;
}

/* -------------------------------- LogDestination ---------------------------------------------- */


class LogDestination {
 public:
  friend class LogMessage;
  friend void ReprintFatalMessage();
  friend base::Logger* base::GetLogger(LogSeverity);
  friend void base::SetLogger(LogSeverity, base::Logger*);

  // 以下方法只是将它们的全局版本进行了转发

  // 设置日志目的文件
  static void SetLogDestination(LogSeverity severity, const char* base_filename);
  // 设置日志文件的符号链接
  static void SetLogSymlink(LogSeverity severity, const char* symlink_filename);
  // 添加日志发送目的地
  static void AddLogSink(LogSink *destination);
  // 删除日志发送目的地
  static void RemoveLogSink(LogSink *destination);
  // 设置日志文件的扩展名
  static void SetLogFilenameExtension(const char* filename_extension);
  // 设置日志输出到标准错误流
  static void SetStderrLogging(LogSeverity min_severity);
  // 设置日志输出到标准错误流(调用SetStderrLogging)
  static void LogToStderr();

  // 刷盘所有日志消息
  static void FlushLogFiles(int min_severity);
  static void FlushLogFilesUnsafe(int min_severity);

  static const string& hostname();
  static const bool& terminal_supports_color() {
    return terminal_supports_color_;
  }

  static void DeleteLogDestinations();

 private:
  LogDestination(LogSeverity severity, const char* base_filename);
  ~LogDestination();

  // 落地特定严重程度的日志消息, 如果它的严重程度足够高，则将其记录到 stderr
  static void MaybeLogToStderr(LogSeverity severity, const char* message, size_t message_len, size_t prefix_len);
  // 落地特定严重程度的日志消息, 如果它的 base filename 不是 "", 则记录到文件
  static void MaybeLogToLogfile(LogSeverity severity, time_t timestamp, const char* message, size_t len);
  // 落地特定严重程度的日志消息, 并将其记录到与该严重程度相对应的文件以及所有严重程度低于此严重程度的文件中
  static void LogToAllLogfiles(LogSeverity severity, time_t timestamp, const char* message, size_t len);
  // 发送日志信息到所有已注册的 sinks
  static void LogToSinks(LogSeverity severity, const char* full_filename, const char* base_filename, int line, 
                         const LogMessageTime& logmsgtime, const char* message, size_t message_len);

  // 等待所有已注册的输出目标通过 WaitTillSent 完成发送
  // 包括 "data" 中的可选目标
  static void WaitForSinks(LogMessage::LogMessageData* data);

  // 返回指定日志类型的 LogDestination
  static LogDestination* log_destination(LogSeverity severity);

  base::Logger* GetLoggerImpl() const { return logger_; }
  void SetLoggerImpl(base::Logger* logger);
  void ResetLoggerImpl() { SetLoggerImpl(&fileobject_); }

  LogFileObject fileobject_;
  base::Logger* logger_; // 是 &fileobject_ 或 继承了 Logger 的类对象
  static std::string hostname_; // 主机名

  // 记录每个日志等级的 LogDestination
  static LogDestination* log_destinations_[NUM_SEVERITIES];
  static bool terminal_supports_color_;

  // 任意的全局日志记录目的地
  static std::vector<LogSink*>* sinks_;

  // 保护 sinks_ , 但不保护 sinks_ 里面的元素所指向的对象
  static std::mutex sink_mutex_;

  // 禁止
  LogDestination(const LogDestination&) = delete;
  LogDestination& operator=(const LogDestination&) = delete;
};

// 静态成员变量的初始化
LogDestination* LogDestination::log_destinations_[NUM_SEVERITIES];
bool LogDestination::terminal_supports_color_ = TerminalSupportsColor();
std::vector<LogSink*>* LogDestination::sinks_ = nullptr;
std::mutex LogDestination::sink_mutex_;
std::string LogDestination::hostname_; 

// 静态函数
const string& LogDestination::hostname() {
  if (hostname_.empty()) {
    GetHostName(&hostname_);
    if (hostname_.empty()) {
      hostname_ = "(unknown)";
    }
  }
  return hostname_;
}

// 私有属性的构造函数, 初始化 日志落地类
LogDestination::LogDestination(LogSeverity severity, const char* base_filename)
  : fileobject_(severity, base_filename), logger_(&fileobject_) {
}
// 析构函数
LogDestination::~LogDestination() {
  ResetLoggerImpl();
}

void LogDestination::SetLoggerImpl(base::Logger* logger) {
  if (logger == logger_) {
    // 防止在重置时释放当前持有的 sink
    return;
  }

  if (logger_ && logger_ != &fileobject_) {
    // 释放用户通过 SetLogger() 指定的 logger 
    delete logger_;
  }
  logger_ = logger;
}

// 刷盘所有日志消息
inline void LogDestination::FlushLogFiles(int min_severity) {
  // 获得锁
  std::lock_guard<std::mutex> lk(log_mutex);
  for (int i = min_severity; i < NUM_SEVERITIES; i++) {
    LogDestination* log = log_destination(i);
    if (log != nullptr) {
      log->logger_->Flush();
    }
  }
}
inline void LogDestination::FlushLogFilesUnsafe(int min_severity) {
  // 假设我们已经持有了锁, 这里不再关心是否持有锁
  for (int i = min_severity; i < NUM_SEVERITIES; i++) {
    LogDestination* log = log_destinations_[i];
    if (log != nullptr) {
      // 直接刷新 fileobject_ logger 而经过任何包装以减少死锁的可能性
      log->fileobject_.FlushUnlocked();
    }
  }
}

// 设置日志目的文件
void LogDestination::SetLogDestination(LogSeverity severity, const char* base_filename) {
  assert(severity >= 0 && severity < NUM_SEVERITIES);
  std::lock_guard<std::mutex> lk(log_mutex);
  log_destination(severity)->fileobject_.SetBasename(base_filename);
}
// 设置日志文件的符号链接
void LogDestination::SetLogSymlink(LogSeverity severity, const char* symlink_filename) {
  assert(severity >= 0 && severity < NUM_SEVERITIES);
  std::lock_guard<std::mutex> lk(log_mutex);
  log_destination(severity)->fileobject_.SetSymlinkBasename(symlink_filename);
}
// 添加日志发送目的地
void LogDestination::AddLogSink(LogSink *destination) {
  std::lock_guard<std::mutex> lk(log_mutex);
  if (!sinks_) { sinks_ = new std::vector<LogSink*>; }
  sinks_->push_back(destination);
}
// 删除日志发送目的地
void LogDestination::RemoveLogSink(LogSink *destination) {
  std::lock_guard<std::mutex> lk(log_mutex);
  if (sinks_) {
    // std::remove() 把指定元素移动到容器末尾, 返回新范围的位置(但会破坏顺序)
    sinks_->erase(std::remove(sinks_->begin(), sinks_->end(), destination), sinks_->end());
  }
}
// 设置日志文件的扩展名
void LogDestination::SetLogFilenameExtension(const char* filename_extension) {
  std::lock_guard<std::mutex> lk(log_mutex);
  for (int i = 0; i < NUM_SEVERITIES; i++) {
    log_destination(i)->fileobject_.SetExtension(filename_extension);
  }
}
// 设置日志输出到标准错误流
void LogDestination::SetStderrLogging(LogSeverity min_severity) {
  assert(min_severity >= 0 && min_severity < NUM_SEVERITIES);
  std::lock_guard<std::mutex> lk(log_mutex);
  FLAGS_stderrthreshold = min_severity;
}
// 设置日志输出到标准错误流(调用SetStderrLogging)
void LogDestination::LogToStderr() {
  // 这里不要获取锁, 因为 SetStderrLogging() 会获取锁 
  SetStderrLogging(0);  // 都输出到 stderr
  for (int i = 0; i < NUM_SEVERITIES; i++) {
    SetLogDestination(i, ""); // "" 表示不写到文件
  }
}

void LogDestination::DeleteLogDestinations() {
  for (auto& log_destination : log_destinations_) {
    delete log_destination;
    log_destination = nullptr;
  }
  std::lock_guard<std::mutex> lk(sink_mutex_);
  delete sinks_;
  sinks_ = nullptr;
}

inline LogDestination* LogDestination::log_destination(LogSeverity severity) {
  assert(severity >= 0 && severity < NUM_SEVERITIES);
  if (!log_destinations_[severity]) {
    log_destinations_[severity] = new LogDestination(severity, nullptr);
  }
  return log_destinations_[severity];
}

// 严重程度颜色匹配
static GLogColor SeverityToColor(LogSeverity severity) {
  assert(severity >= 0 && severity < NUM_SEVERITIES);
  GLogColor color = COLOR_DEFAULT;
  switch(severity) {
  case GLOG_INFO:
    color = COLOR_DEFAULT;
    break;
  case GLOG_WARNING:
    color = COLOR_YELLOW;
    break;
  case GLOG_ERROR:
  case GLOG_FATAL:
    color = COLOR_RED;
    break;
  default:
    // 永远不会执行这个分支
    assert(false);
  }
  return color;
}

// 颜色的 ANSI 转义码
static const char* GetAnsiColorCode(GLogColor color) {
  switch (color) {
  case COLOR_RED:     return "1";
  case COLOR_GREEN:   return "2";
  case COLOR_YELLOW:  return "3";
  case COLOR_DEFAULT: return "";
  };
  return nullptr;
}

// 把带颜色的日志写到 stderr or stdout
static void ColoredWriteToStderrOrStdout(FILE* output, LogSeverity severity, const char* message, size_t len) {
  bool is_stdout = (output == stdout);
  const GLogColor color = (LogDestination::terminal_supports_color() &&
                          ((!is_stdout && FLAGS_colorlogtostderr) ||
                           (is_stdout && FLAGS_colorlogtostdout))) ? 
                           SeverityToColor(severity) : COLOR_DEFAULT;

  if (color == COLOR_DEFAULT) {
    // 避免在此模块中使用 std::cerr，因为这个函数可能会在退出代码期间被调用,
    // 并且那时 std::cerr 可能会部分或完全销毁
    fwrite(message, len, 1, output);
    return;
  }

  fprintf(output, "\033[0;3%sm", GetAnsiColorCode(color));
  fwrite(message, len, 1, output);
  fprintf(output, "\033[m"); // 恢复原来的颜色
}

// 把带颜色的日志写到 stdout
static void ColoredWriteToStdout(LogSeverity severity, const char* message, size_t len) {
  FILE* output = stdout;
  // 还需要判断是否是 stderr
  if (severity >= FLAGS_stderrthreshold) {
    output = stderr;
  }
  ColoredWriteToStderrOrStdout(output, severity, message, len);
}
// 把带颜色的日志写到 stderr
static void ColoredWriteToStderr(LogSeverity severity, const char* message, size_t len) {
  ColoredWriteToStderrOrStdout(stderr, severity, message, len);
}

static void WriteToStderr(const char* message, size_t len) {
  // 避免在此模块中使用 std::cerr，因为这个函数可能会在退出代码期间被调用,
  // 并且那时 std::cerr 可能会部分或完全销毁
  fwrite(message, len, 1, stderr); // 把 n 个 len 的数据块写到文件
}

// 落地特定严重程度的日志消息, 如果它的严重程度足够高，则将其记录到 stderr
void LogDestination::MaybeLogToStderr(LogSeverity severity, const char* message, size_t message_len, size_t prefix_len) {
  if (severity >= FLAGS_stderrthreshold || FLAGS_alsologtostderr) {
    ColoredWriteToStderr(severity, message, message_len);
    (void) prefix_len; // 空语句, 用于避免编译器发出未使用变量的警告
  }
}

// 落地特定严重程度的日志消息, 如果它的 base filename 不是 "", 则记录到文件
void LogDestination::MaybeLogToLogfile(LogSeverity severity, time_t timestamp, const char* message, size_t len) {
  const bool should_flush = severity > FLAGS_logbuflevel;
  LogDestination* destination = log_destination(severity);
  destination->logger_->Write(should_flush, timestamp, message, len); // 日志落地
}

// 落地特定严重程度的日志消息, 并将其记录到与该严重程度相对应的文件以及所有严重程度低于此严重程度的文件中
void LogDestination::LogToAllLogfiles(LogSeverity severity, time_t timestamp, const char* message, size_t len) {
  if (FLAGS_logtostdout) {
    // 直接写到 stdout
    ColoredWriteToStdout(severity, message, len);
  } else if (FLAGS_logtostderr) {
    // 直接写到 stderr
    ColoredWriteToStderr(severity, message, len);
  } else {
    for (int i = severity; i >= 0; --i) {
      LogDestination::MaybeLogToLogfile(i, timestamp, message, len);
    }
  }
}

// 发送日志信息到所有已注册的 sinks
void LogDestination::LogToSinks(LogSeverity severity, const char* full_filename, const char* base_filename, int line, 
                        const LogMessageTime& logmsgtime, const char* message, size_t message_len) {
  
  // TODO: 增加 读写锁提高读多写少的并发量
  std::lock_guard<std::mutex> lk(sink_mutex_);
  if (sinks_) {
    for (size_t i = sinks_->size(); i-- > 0; ) {
      // i-- 是因为 size_t 是 unsigned
      // 发送日志到已注册的 sink 
      (*sinks_)[i]->send(severity, full_filename, base_filename, line, logmsgtime, message, message_len);
    }
  }
}

// 等待所有已注册的输出目标通过 WaitTillSent 完成发送
// 包括 "data" 中的可选目标
void LogDestination::WaitForSinks(LogMessage::LogMessageData* data) {
  std::lock_guard<std::mutex> lk(sink_mutex_);
  if (sinks_) {
    for (size_t i = sinks_->size(); i-- > 0; ) {
      // i-- 是因为 size_t 是 unsigned
      // 等待发送日志到已注册的 sink 
      (*sinks_)[i]->WaitTillSent();
    }
  }

  const bool send_to_sink = (data->send_method_ == &LogMessage::SendToSink) || 
                            (data->send_method_ == &LogMessage::SendToSinkAndLog);
  
  if (send_to_sink && data->sink_ != nullptr) {
    data->sink_->WaitTillSent();
  }

}


/* ---------------------------------- LogDestination end -------------------------------------------- */

/* ---------------------------------- LogFileObject -------------------------------------------- */

namespace {
// 文件目录分隔符号
const char possible_dir_delim[] = {'/'};

LogFileObject::LogFileObject(LogSeverity severity, const char* base_filename)
 : base_filename_selected_(base_filename != nullptr),
   base_filename_((base_filename != nullptr) ? base_filename : nullptr),
   symlink_basename_(glog_internal_namespace_::ProgramInvocationShortName()),
   filename_extension_(),
   severity_(severity),
   rollover_attempt_(kRolloverAttemptFrequency - 1),
   start_time_(glog_internal_namespace_::WallTime_Now())
    {
  assert(severity >= 0 && severity < NUM_SEVERITIES);
}

LogFileObject::~LogFileObject() {
  std::lock_guard<std::mutex> lk(lock_);
  if (file_ != nullptr) {
    fclose(file_);
    file_ = nullptr;
  }
}

void LogFileObject::SetBasename(const char* basename) {
  std::lock_guard<std::mutex> lk(lock_);
  base_filename_selected_ = true;
  if (base_filename_ != basename) {
    // 正在改名字, 旧日志关闭
    if (file_ != nullptr) {
      fclose(file_);
      file_ = nullptr;
      rollover_attempt_ = kRolloverAttemptFrequency - 1;
    }
    base_filename_ = basename;
  }  
}

void LogFileObject::SetExtension(const char* ext) {
  std::lock_guard<std::mutex> lk(lock_);
  base_filename_selected_ = true;
  if (filename_extension_ != ext) {
    // 正在改名字, 旧日志关闭
    if (file_ != nullptr) {
      fclose(file_);
      file_ = nullptr;
      rollover_attempt_ = kRolloverAttemptFrequency - 1;
    }
    filename_extension_ = ext;
  } 
}

void LogFileObject::SetSymlinkBasename(const char* symlink_basename) {
  std::lock_guard<std::mutex> lk(lock_);
  symlink_basename_ = symlink_basename;
}

void LogFileObject::Flush() {
  std::lock_guard<std::mutex> lk(lock_);
  FlushUnlocked();
}

void LogFileObject::FlushUnlocked() {
  if (file_ != nullptr) {
    fflush(file_); // sys func
    bytes_since_flush_ = 0;
  }

  const int64 next = (FLAGS_logbufsecs * static_cast<int64>(1000000)); // 毫秒
  next_flush_time_ = glog_internal_namespace_::CycleClock_Now() + glog_internal_namespace_::UsecToCycles(next); 
}

bool LogFileObject::CreateLogfile(const std::string& time_pid_string) {
  std::string string_filename = base_filename_;
  if (FLAGS_timestamp_in_logfile_name) {
    string_filename += time_pid_string;
  }

  string_filename += filename_extension_; // 扩展名

  const char* filename = string_filename.c_str();
  // 只写 且 不存在时创建
  int flags = O_WRONLY | O_CREAT;
  if (FLAGS_timestamp_in_logfile_name) {
    // 如果文件已存在则会失败
    flags = flags | O_EXCL;
  }
  // 打开文件
  int fd = open(filename, flags, static_cast<mode_t>(FLAGS_logfile_mode));
  if (fd == -1) return false;

  // fd 与一个 流 关联
  file_ = fdopen(fd, "a");
  if (file_ == nullptr) {
    close(fd);
    if (FLAGS_timestamp_in_logfile_name) {
      unlink(filename); // 删除创建的文件
    }
    return false;
  }

  // 创建一个 名为 <program_name>.<severity> 的软链接
  // 每次我们创建一个新的日志文件, 我们都会删除旧的软链接并创建一个新的, 使得它一直指向新的日志文件
  if (!symlink_basename_.empty()) {
    const char* slash = strrchr(filename, PATH_SEPARATOR);
    // 软链接名
    const std::string linkname = symlink_basename_ + '.' + LogSeverityNames[severity_];
    std::string linkpath;
    if (slash) {
      // 获取目录名
      linkpath = std::string(filename, static_cast<size_t>(slash - filename + 1));
    }
    linkpath += linkname;
    unlink(linkpath.c_str()); // 删除旧的软链接

    // 包含 unistd.h
    // 使符号链接相对于当前目录(在相同目录内)以便与整个日志目录被移动时仍然有效
    const char* linkdest = slash ? (slash + 1) : filename;
    // 此时 linkpath --> linkdest
    if (symlink(linkdest, linkpath.c_str()) != 0) {
      // 忽略错误
    }

    // 根据 FLAGS 创建一个指定的软链接
    if (!FLAGS_log_link.empty()) {
      linkpath = FLAGS_log_link + "/" + linkname;
      unlink(linkpath.c_str()); // 删除旧的软链接
      if (symlink(filename, linkpath.c_str()) != 0) {
        // 忽略错误
      }
    }
  }
  return true;
}

void LogFileObject::Write(bool force_flush, time_t timestamp, const char* message, size_t message_len) {
  std::lock_guard<std::mutex> lk(lock_);
  // base_filename_ 是空则不用写
  if (base_filename_selected_ && base_filename_.empty()) {
    return;
  }

  // file_length_ >> 20U 相当于把字节数转化从兆 B --> MB
  if (file_length_ >> 20U >= MaxLogSize() || glog_internal_namespace_::PidHasChanged()) {
    if (file_ != nullptr) fclose(file_);
    file_ = nullptr;
    file_length_ = bytes_since_flush_ = dropped_mem_length_ = 0;
    rollover_attempt_ = kRolloverAttemptFrequency - 1;
  }
  // 如果文件还没创建就先创建
  if (file_ == nullptr) {
    // 每 32 条日志更新一次日志文件
    // 只有在创建文件时出现问题才会执行此处, 会丢失日志!
    if (++rollover_attempt_ != kRolloverAttemptFrequency) return;
    rollover_attempt_ = 0;

    struct ::tm tm_time;
    if (FLAGS_log_utc_time) {
      gmtime_r(&timestamp, &tm_time);
    } else {
      // 将时间戳转换为本地时间存在 tm_time
      localtime_r(&timestamp, &tm_time);
    }

    // 文件名包括 日期/时间 pid 
    std::ostringstream time_pid_stream;
    time_pid_stream.fill('0');
    time_pid_stream << 1900+tm_time.tm_year
                    << setw(2) << 1 + tm_time.tm_mon
                    << setw(2) << tm_time.tm_mday
                    << '-'
                    << setw(2) << tm_time.tm_hour
                    << setw(2) << tm_time.tm_min
                    << setw(2) << tm_time.tm_sec
                    << '.'
                    << glog_internal_namespace_::GetMainThreadPid();
    const std::string& time_pid_string = time_pid_stream.str();

    if (base_filename_selected_) {
      if (!CreateLogfile(time_pid_string)) {
        perror("Could not create log file");
        fprintf(stderr, "COULD NOT CREATE LOGFILE '%s'!\n", time_pid_string.c_str());
        return;
      }
    } else {
      // 对于这个 severity_ 如果没有指定的 base_filename, 则使用默认的 base_filename
      // 默认名称: <program name>.<hostname>.<user name>.log.<severity level> 

      std::string stripped_filename(glog_internal_namespace_::ProgramInvocationShortName());
      std::string hostname;
      GetHostName(&hostname);

      std::string uidname = glog_internal_namespace_::MyUserName();
      if (uidname.empty()) uidname = "invalid-user";

      stripped_filename = stripped_filename + '.' + hostname + '.' + uidname + ".log" + LogSeverityNames[severity_]+'.';

      // 我们可能将日志放在不同的目录
      const std::vector<string>& log_dirs = GetLoggingDirectories();

      bool success = false;
      for (const auto& log_dir : log_dirs) {
        base_filename_ = log_dir + "/" + stripped_filename;
        if ( CreateLogfile(time_pid_string)) {
          success = true;
          break;
        }
      }

      if (success == false) {
        perror("Could not create log file");
        fprintf(stderr, "COULD NOT CREATE LOGFILE '%s'!\n", time_pid_string.c_str());
        return;
      }
    }

    if (FLAGS_log_file_header) {
      std::ostringstream file_header_stream;
      file_header_stream.fill('0');
      file_header_stream << "Log file created at: "
                         << 1900+tm_time.tm_year << '/'
                         << setw(2) << 1+tm_time.tm_mon << '/'
                         << setw(2) << tm_time.tm_mday << ' '
                         << setw(2) << tm_time.tm_hour << ':'
                         << setw(2) << tm_time.tm_min << ':'
                         << setw(2) << tm_time.tm_sec << (FLAGS_log_utc_time ? " UTC\n" : "\n")
                         << "Running on machine: "
                         << LogDestination::hostname() << '\n';
      const char* const date_time_format = FLAGS_log_year_in_prefix ? "yyyy-mm-dd hh:mm:ss.uuuuuu" : "mm-dd hh:mm:ss.uuuuuu";
      // `2023-10-08 17:13:08.888917 [webserver.cpp:36][info]: `
      file_header_stream << "Running duration (h:mm:ss): "
                         << PrettyDuration(static_cast<int>(glog_internal_namespace_::WallTime_Now() - start_time_)) << '\n'
                         << "Log line format: [IWEF]" << date_time_format << " "
                         << "[file:line][severity]: msg" << '\n';
      const string& file_header_string = file_header_stream.str();

      const size_t header_len = file_header_string.size();
      fwrite(file_header_string.data(), 1, header_len, file_);
      file_length_ += header_len;
      bytes_since_flush_ += header_len;
    }
  }

  // 写到文件
  if (!stop_writing) {
    // 当磁盘已满时, fwrite() 对于小于 4096 字节的消息不会返回错误。
    // 对于小于 4096 字节的消息, 它会返回消息的长度. 对于大于 4096 字节的消息, fwrite() 会返回 4096,从而表示发生了错误。
    errno = 0;
    fwrite(message, 1, message_len, file_);
    if ( FLAGS_stop_logging_if_full_disk && errno == ENOSPC) {
      // 磁盘不足
      stop_writing = true;
      return;
    } else {
      file_length_ += message_len;
      bytes_since_flush_ += message_len;
    }
  } else {
    if (glog_internal_namespace_::CycleClock_Now() >= next_flush_time_) {
      stop_writing = false; // 需要刷新了
    }
    return; // 不需要刷盘
  }

  if ( force_flush || (bytes_since_flush_ >= 1000000) || 
      (glog_internal_namespace_::CycleClock_Now() >= next_flush_time_)) {
    FlushUnlocked();
    // Linux
    // 如果文件长度大于 3MB, 则释放一些文件流中的内存
    if (FLAGS_drop_log_memory && file_length_ >= (3U << 20U)) {
      // 对file_length_低 20 位清零(即取整比如 4.2M 取整到 4MB) 并且 - 1M
      // 最后日志文件只保留 1～2M
      uint32 total_drop_length = (file_length_ & ~((1U << 20U) - 1U)) - (1U << 20U);
      uint32 this_drop_length = total_drop_length - dropped_mem_length_;
      if (this_drop_length >= (2U << 20U)) {
        // 建议系统释放相关数据块的缓存
        // fileno(file_) 获得描述符
        // static_cast<off_t>(dropped_mem_length_) 文件偏移量
        // static_cast<off_t>(this_drop_length) 要释放的内存
        // POSIX_FADV_DONTNEED: 建议系统释放相关数据块的缓存
        posix_fadvise(fileno(file_), static_cast<off_t>(dropped_mem_length_), 
                      static_cast<off_t>(this_drop_length), POSIX_FADV_DONTNEED);
        
        dropped_mem_length_ = total_drop_length;
      }
    }
  }

  // 删除旧的日志
  if (log_cleaner.enabled()) {
    log_cleaner.Run(base_filename_selected_, base_filename_, filename_extension_);
  }
}

LogCleaner::LogCleaner() = default;

void LogCleaner::Enable(unsigned int overdue_days) {
  enabled_ = true;
  overdue_days_ = overdue_days;
}

void LogCleaner::Disable() {
  enabled_ = false;
}

void LogCleaner::UpdateCleanUpTime() {
  const int64 next = (FLAGS_logcleansecs * 1000000); // usec
  next_cleanup_time_ = glog_internal_namespace_::CycleClock_Now() + glog_internal_namespace_::UsecToCycles(next);
}

void LogCleaner::Run(bool base_filename_selected, 
                     const std::string& base_filename, 
                     const std::string& filename_extension) {
  
  assert(enabled_);
  assert(!base_filename_selected || !base_filename.empty());

  // 避免 扫描日志太频繁
  if (glog_internal_namespace_::CycleClock_Now() < next_cleanup_time_) {
    return;
  }

  UpdateCleanUpTime();

  std::vector<std::string> dirs;

  if (!base_filename_selected) {
    dirs = GetLoggingDirectories();
  } else {
    size_t pos = base_filename.find_last_of(possible_dir_delim, std::string::npos, sizeof(possible_dir_delim));

    if (pos != std::string::npos) {
      std::string dir = base_filename.substr(0, pos + 1);
      dirs.push_back(dir);
    } else {
      dirs.emplace_back(".");
    }
  }

  for (auto& dir : dirs) {
    std::vector<std::string> logs = GetOverdueLogNames(dir, overdue_days_, base_filename, filename_extension);

    for (auto& log : logs) {
      static_cast<void>(unlink(log.c_str())); // 删除文件
    }
  }

}


std::vector<std::string> LogCleaner::GetOverdueLogNames(std::string log_directory, unsigned int days,
                                                const std::string& base_filename,
                                                const std::string& filename_extension) const {
  
  // 过期的日志
  std::vector<std::string> overdue_log_names;

  // 尝试获取在日志目录下的所有文件
  DIR* dir;            // 目录结构体
  struct dirent* ent;  // 目录项结构体(子目录)

  if ((dir = opendir(log_directory.c_str()))) {
    while ((ent = readdir(dir))) {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
        // 跳过两个特殊的子目录
        continue;
      }

      std::string filepath = ent->d_name;
      // 尾指针
      const char* const dir_delim_end = possible_dir_delim + sizeof(possible_dir_delim);
      // 判断 log_directory 的结尾是否是文件夹结尾
      if (!log_directory.empty() && std::find(possible_dir_delim, dir_delim_end, log_directory.back()) != dir_delim_end) {
        filepath = log_directory + filepath;
      }
      // 
      if (IsLogFromCurrentProject(filepath, base_filename, filename_extension) && 
          IsLogLastModifiedOver(filepath, days)) {
        overdue_log_names.push_back(filepath);
      }
    }
    closedir(dir);
  }

  return overdue_log_names;
}


bool LogCleaner::IsLogFromCurrentProject(const std::string& filepath,
                                 const std::string& base_filename,
                                 const std::string& filename_extension) const {

  // 移除 base_filename 多余的 '/'
  // 原来 "/tmp//<base_filename>.<create_time>.<pid>"
  // 移除 "/tmp/<base_filename>.<create_time>.<pid>"
  std::string cleaned_base_filename;

  const char* const dir_delim_end = possible_dir_delim + sizeof(possible_dir_delim);

  size_t real_filepath_size = filepath.size();
  for (char c : base_filename) {
    if (cleaned_base_filename.empty()) {
      cleaned_base_filename += c;
    } else if (std::find(possible_dir_delim, dir_delim_end, c) == dir_delim_end || 
               (!cleaned_base_filename.empty() && c != cleaned_base_filename.back())) {
      
      cleaned_base_filename += c;
    }
  }

  // 如果 filename 不以 cleaned_base_filename 开头就返回
  if (filepath.find(cleaned_base_filename) != 0) {
    return false;
  }

  // 如果设置了 filename_extension, 则检查 filename_extension 是否在 cleaned_base_filename 的相邻右边
  if (!filename_extension.empty()) {
    if (cleaned_base_filename.size() >= real_filepath_size) {
      return false;
    }
    // 对于原始版本, filename_extension 在 filepath 的中间
    std::string ext = filepath.substr(cleaned_base_filename.size(), filename_extension.size());
    if (ext == filename_extension) {
      cleaned_base_filename += filename_extension;
    } else {
      // 对于新版本, filename_extension 在 filepath 的尾部
      if (filename_extension.size() >= real_filepath_size) {
        return false;
      }
      real_filepath_size = filepath.size() - filename_extension.size();
      if (filepath.substr(real_filepath_size) != filename_extension) {
        return false;
      }
    }
  }

  // YYYYMMDD-HHMMSS.pid
  for (size_t i = cleaned_base_filename.size(); i < real_filepath_size; i++) {
    const char& c = filepath[i];

    if (i <= cleaned_base_filename.size() + 7) {
      // 0~7: YYYYMMDD
      if (c < '0' || c > '9') {return false;}
    } else if (i == cleaned_base_filename.size() + 8) {
      // 8: -
      if (c != '-') {return false;}
    } else if (i <= cleaned_base_filename.size() + 14) {
      // 9~14: HHMMSS
      if (c < '0' || c > '9') {return false;}
    } else if (i == cleaned_base_filename.size() + 15) {
      // 15: .
      if (c != '.') {return false;}
    } else if (i >= cleaned_base_filename.size() + 16) {
      // 16+: pid
      if (c < '0' || c > '9') {return false;}
    }
  }
  return true;
}

bool LogCleaner::IsLogLastModifiedOver(const std::string& filepath, unsigned int days) const {
  // 获取文件的上次修改时间
  struct stat file_stat;

  if (stat(filepath.c_str(), &file_stat) == 0) {
    const time_t seconds_in_a_day = 60 * 60 * 24;
    time_t last_modified_time = file_stat.st_mtime;
    time_t current_time = time(nullptr);
    return difftime(current_time, last_modified_time) > days * seconds_in_a_day; // 是否过期
  }
  return false;
}


} // end of namespace


/* ---------------------------------- LogFileObject end -------------------------------------------- */

// 因为多个线程可能同时调用 LOG(FATAL), 我们需要保留第一个 FATAL 信息
// 申请两个 log data 空间, 一个由第一个线程独享, 一个由所有其他线程共享
static std::mutex fatal_msg_lock;
static glog_internal_namespace_::CrashReason crash_reason;
static bool fatal_msg_exclusive = true;
static LogMessage::LogMessageData fatal_msg_data_exclusive;
static LogMessage::LogMessageData fatal_msg_data_shared;

LogMessage::LogMessageData::LogMessageData()
  : stream_(message_text_, LogMessage::kMaxLogMessageLen, 0) { 
  // 初始化 LogStream
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity, int64 ctr, SendMethod send_method)
    : allocated_(nullptr) {
  Init(file, line, severity, send_method);
  data_->stream_.set_ctr(ctr);
}

LogMessage::LogMessage(const char* file, int line, const CheckOpString& result)
    : allocated_(nullptr) {
  Init(file, line, GLOG_FATAL, &LogMessage::SendToLog);
  stream() << "check failed: " << (*result.str_) << " ";
}

LogMessage::LogMessage(const char* file, int line) : allocated_(nullptr) {
  Init(file, line, GLOG_INFO, &LogMessage::SendToLog);
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity)
    : allocated_(nullptr) {
  Init(file, line, severity, &LogMessage::SendToLog);
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity, LogSink* sink, bool also_send_to_log)
    : allocated_(nullptr) {
  Init(file, line, severity, also_send_to_log ? &LogMessage::SendToSinkAndLog : 
                                                &LogMessage::SendToLog);
  data_->sink_ = sink;                                                
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity, std::vector<std::string>* outvec)
    : allocated_(nullptr) {
  Init(file, line, severity, &LogMessage::SaveOrSendToLog);
  data_->outvec_ = outvec;
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity, std::string* message) {
  Init(file, line, severity, &LogMessage::WriteToStringAndLog);
  data_->message_ = message;
}

void LogMessage::Init(const char* file, int line, LogSeverity severity, void (LogMessage::*send_method)()) {
  allocated_ = nullptr;
  if (severity != GLOG_FATAL) {
    allocated_ = new LogMessageData();
    data_ = allocated_;
    data_->first_fatal_ = false;
  } else {
    std::lock_guard<std::mutex> lk(fatal_msg_lock);
    if (fatal_msg_exclusive) {
      // 第一个 FATAL msg
      fatal_msg_exclusive = false;
      data_ = &fatal_msg_data_exclusive;
      data_->first_fatal_ = true;
    } else {
      data_ = &fatal_msg_data_shared;
      data_->first_fatal_ = false;
    }
  }

  data_->preserved_errno_ = errno;
  data_->severity_ = severity;
  data_->line_ = line;
  data_->send_method_ = send_method;
  data_->sink_ = nullptr;
  data_->outvec_ = nullptr;
  data_->message_ = nullptr; // ??: add message_ nullptr
  WallTime now = glog_internal_namespace_::WallTime_Now();
  time_t timestamp_now = static_cast<time_t>(now);
  logmsgtime_ = LogMessageTime(timestamp_now, now);

  data_->num_chars_to_log_ = 0;
  data_->num_chars_to_syslog_ = 0;
  data_->basename_ = glog_internal_namespace_::const_basename(file);
  data_->fullname_ = file;
  data_->has_been_flushed_ = false;

  // 添加日志前缀
  if (line != kNoLogPrefix) {
    std::ios saved_fmt(nullptr);
    saved_fmt.copyfmt(stream()); // 先保存原来的流格式
    stream().fill('0');

    // TODO: 增加一个回调函数扩展接口, 可以让用户自定义前缀格式
    // TODO: 根据 FLAGS 决定是否记录年
    // 以下是写死的格式
    // `2023-10-08 17:13:08.888917 [webserver.cpp:36][info]: `
    stream() << setw(4) << 1900 + logmsgtime_.year() << "-" 
             << setw(2) << 1 + logmsgtime_.month() << "-" 
             << setw(2) << logmsgtime_.day() << ' '
             << setw(2) << logmsgtime_.hour() << ':'
             << setw(2) << logmsgtime_.min() << ':'
             << setw(2) << logmsgtime_.sec() << "."
             << setw(6) << logmsgtime_.usec() << " ["
             << data_->basename_ << ':' << data_->line_ << "]["
             << LogSeverityNames[severity] << "]: ";

    stream().copyfmt(saved_fmt); // 替换回原来的流格式
  }

  data_->num_prefix_chars_ = data_->stream_.pcount();
}

const LogMessageTime& LogMessage::getLogMessageTime() const {
  return logmsgtime_;
}

LogMessage::~LogMessage() {
  Flush();
  delete allocated_;
}

int LogMessage::preserved_errno() const {
  return data_->preserved_errno_;
}

std::ostream& LogMessage::stream() {
  return data_->stream_;
}

void LogMessage::Flush() {
  if (data_->has_been_flushed_  || data_->severity_ < FLAGS_minloglevel ) {
    return;
  }

  data_->num_chars_to_log_ = data_->stream_.pcount();
  data_->num_chars_to_syslog_ = data_->num_chars_to_log_ - data_->num_prefix_chars_;

  bool append_newline = (data_->message_text_[data_->num_chars_to_log_-1] != '\n');
  char original_final_char = '\0'; // 用来保存原来的最后一个字符

  // 直接修改 stream_ 缓冲区
  if (append_newline) {
    original_final_char = data_->message_text_[data_->num_chars_to_log_];
    data_->message_text_[data_->num_chars_to_log_++] = '\n';
  }
  data_->message_text_[data_->num_chars_to_log_] = '\0';

  {
    std::lock_guard<std::mutex> lk(log_mutex);
    (this->*(data_->send_method_))(); // 执行回调函数(日志发送下一步处理)
    ++num_messages_[static_cast<int>(data_->severity_)];
  }
  LogDestination::WaitForSinks(data_);

  if (append_newline) {
    // 恢复到换行符增加前
    data_->message_text_[data_->num_chars_to_log_ - 1] = original_final_char;
  }

  // 如果在日志调用前 errno 已经被设置了, 那么在日志记录后不能改变 errno, 需要设置回原来的值
  if (data_->preserved_errno_ != 0) {
    errno = data_->preserved_errno_;
  }

  data_->has_been_flushed_ = true;
}


// 复制第一个致命错误日志消息, 以便我们在所有堆栈跟踪之后可以再次打印它出来.
// 为了保持与旧版行为的一致, 我们不使用 fatal_msg_data_exclusive
static time_t fatal_time;
static char fatal_message[256];


// 重复打印致命错误信息
void ReprintFatalMessage() {
  if (fatal_message[0]) {
    const size_t n = strlen(fatal_message);
    if (!FLAGS_logtostderr) {
      // 避免重复打印
      WriteToStderr(fatal_message, n);
    }
    LogDestination::LogToAllLogfiles(GLOG_ERROR, fatal_time, fatal_message, n);
  }
}

// 必须持有 log_mutex
void LogMessage::SendToLog() {
  static bool already_warned_before_initgoolgle = false;
  // 确保持有锁
  
  assert(data_->num_chars_to_log_ > 0 && data_->message_text_[data_->num_chars_to_log_ - 1] == '\n');

  if (!already_warned_before_initgoolgle && !IsGoogleLoggingInitialized()) {
    const char w[] = "WARNING: Logging before InitGoogleLogginging() is written to STDERR\n";
    WriteToStderr(w, strlen(w));
    already_warned_before_initgoolgle = true;
  }

  if (FLAGS_logtostderr || FLAGS_logtostdout || !IsGoogleLoggingInitialized()) {
    if (FLAGS_logtostdout) {
      ColoredWriteToStdout(data_->severity_, data_->message_text_, data_->num_chars_to_log_);
    } else {
      ColoredWriteToStderr(data_->severity_, data_->message_text_, data_->num_chars_to_log_);
    }

    // 如果有需要这里可以用 FLAG 保护起来
    // 不发送头部
    LogDestination::LogToSinks(data_->severity_, data_->fullname_, data_->basename_,
                              data_->line_, logmsgtime_, data_->message_text_ + data_->num_prefix_chars_,
                              (data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1) );

  } else {
    // 把日志文件落地
    LogDestination::LogToAllLogfiles(data_->severity_, logmsgtime_.timestamp(), 
                                    data_->message_text_, data_->num_chars_to_log_);

    LogDestination::MaybeLogToStderr(data_->severity_, data_->message_text_, 
                                    data_->num_chars_to_log_, data_->num_prefix_chars_);
    
    LogDestination::LogToSinks(data_->severity_, data_->fullname_, data_->basename_,
                              data_->line_, logmsgtime_, data_->message_text_ + data_->num_prefix_chars_,
                              (data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1) );
  }

  // 如果我们记录了一个致命错误的消息, 将所有的日志输出刷新一遍
  // 然后发送一个信号让其他人来处理. 我们保持日志处于一种状态, 其他人可以使用它们(只要在之后也进行了刷新)
  if (data_->severity_ == GLOG_FATAL && exit_on_dfatal) {
    if (data_->first_fatal_) {
      // 保存错误信息
      RecordCrashReason(&crash_reason);
      glog_internal_namespace_::SetCrashReason(&crash_reason);

      // 保存最短的错误信息
      const size_t copy = std::min(data_->num_chars_to_log_, sizeof(fatal_message) - 1);
      memcpy(fatal_message, data_->message_text_, copy);
      fatal_message[copy] = '\0';
      fatal_time = logmsgtime_.timestamp();
    }

    if (!FLAGS_logtostderr && !FLAGS_logtostdout) {
      for (auto& log_destination : LogDestination::log_destinations_) {
        if (log_destination) {
          log_destination->logger_->Write(true, 0, "", 0);
        }
      }
    }

    // 释放锁, 因为这里是 fatal 信息, 进程会被结束
    log_mutex.unlock();
    LogDestination::WaitForSinks(data_);

    const char* message = "*** Check failure stack trace: ***\n";
    if (write(STDERR_FILENO, message, strlen(message)) < 0) {
      // Ignore errors.
    }

    // 结束进程
    Fail();
  }

}

void LogMessage::RecordCrashReason(glog_internal_namespace_::CrashReason* reason) {
  reason->filename = fatal_msg_data_exclusive.fullname_;
  reason->line_number = fatal_msg_data_exclusive.line_;
  reason->message = fatal_msg_data_exclusive.message_text_ + fatal_msg_data_exclusive.num_prefix_chars_; // 不记录头部
  reason->depth = 0;
}

// 程序 crash 时调用的函数(默认是 abort() )
logging_fail_func_t g_logging_fail_func = reinterpret_cast<logging_fail_func_t>(&abort);
// 修改 程序 crash 时调用的函数
void InstallFailureFunction(logging_fail_func_t fail_func) {
  g_logging_fail_func = fail_func;
}

void LogMessage::Fail() {
  g_logging_fail_func();
}

// 需要确保持有锁 log_mutex
void LogMessage::SendToSink() {
  // 确保持有锁
  if (data_->sink_ != nullptr) {
    assert(data_->num_chars_to_log_ > 0 && data_->message_text_[data_->num_chars_to_log_ - 1] == '\n');

    data_->sink_->send(data_->severity_, data_->fullname_, data_->basename_, data_->line_,
                      logmsgtime_, data_->message_text_ + data_->num_prefix_chars_, (data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1));

  }
}

// 确保持有锁
void LogMessage::SendToSinkAndLog() {
  SendToSink();
  SendToLog();
}

// 确保持有锁
void LogMessage::SaveOrSendToLog() {
  if (data_->outvec_ != nullptr) {
    assert(data_->num_chars_to_log_ > 0 && data_->message_text_[data_->num_chars_to_log_ - 1] == '\n');

    // 类型转换
    const char* start = data_->message_text_ + data_->num_prefix_chars_;
    size_t len = data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1;
    data_->outvec_->push_back(std::string(start, len));
  } else {
    SendToLog();
  }
}

// 确保持有锁
void LogMessage::WriteToStringAndLog() {
  if (data_->message_ != nullptr) {
    assert(data_->num_chars_to_log_ > 0 && data_->message_text_[data_->num_chars_to_log_ - 1] == '\n');

    // 类型转换
    const char* start = data_->message_text_ + data_->num_prefix_chars_;
    size_t len = data_->num_chars_to_log_ - data_->num_prefix_chars_ - 1;
    data_->message_->assign(start, len);
  } 
}

void LogMessage::SendToSyslogAndLog() {
  // TODO: 增加 LOG() 宏接口
  // LOG(ERROR) << "No syslog support: message=" << data_->message_text_;
}

// 静态成员函数
int64 LogMessage::num_messages(int severity) {
  std::lock_guard<std::mutex> lk(log_mutex);
  return num_messages_[severity];
}




// 获取指定严重程度级别的日志记录器
// 日志记录器仍然属于日志模块的所有权, 不应由调用者删除
// 线程安全的
base::Logger* base::GetLogger(LogSeverity level) {
  std::lock_guard<std::mutex> lk(log_mutex);
  return LogDestination::log_destination(level)->GetLoggerImpl();
}

// 设置指定严重程度级别的日志记录器
// 日志记录器将成为日志模块的所有权, 调用者不应该删除它
// 线程安全的
void base::SetLogger(LogSeverity level, base::Logger* logger) {
  std::lock_guard<std::mutex> lk(log_mutex);
  LogDestination::log_destination(level)->SetLoggerImpl(logger);
}

// 当前仅当 ostream 是 LogStream 时起作用
std::ostream& operator<<(std::ostream& os, const PRIVATE_Counter&) {
  auto* log = dynamic_cast<LogMessage::LogStream*>(&os);
  assert(log && log == log->self());
  os << log->ctr();
  return os;
}

/* ----------------------------- LogMessageTime ---------------------------- */
LogMessageTime::LogMessageTime()
  : time_struct_(), timestamp_(0), usecs_(0), gmtoffset_(0) {}

LogMessageTime::LogMessageTime(std::tm t) {
  std::time_t timestamp = std::mktime(&t);
  init(t, timestamp, 0);
}

LogMessageTime::LogMessageTime(std::time_t timestamp, WallTime now) {
  std::tm t;
  if (FLAGS_log_utc_time) {
    gmtime_r(&timestamp, &t);
  } else {
    localtime_r(&timestamp, &t);
  }
  init(t, timestamp, now);
}

void LogMessageTime::init(const std::tm& t, std::time_t timestamp, WallTime now) {
  time_struct_ = t;
  timestamp_ = timestamp;
  usecs_ = static_cast<int32>((now - timestamp) * 1000000);

  CalcGmtOffset();
}

void LogMessageTime::CalcGmtOffset() {
  std::tm gmt_struct;
  int isDst = 0;
  if (FLAGS_log_utc_time) {
    localtime_r(&timestamp_, &gmt_struct);
    isDst = gmt_struct.tm_isdst;
    gmt_struct = time_struct_;
  } else {
    isDst = time_struct_.tm_isdst;
    gmtime_r(&timestamp_, &gmt_struct);
  }

  time_t gmt_sec = mktime(&gmt_struct);
  const long hour_secs = 3600;
  gmtoffset_ = static_cast<long int>(timestamp_ - gmt_sec + (isDst ? hour_secs : 0));
}


/* ----------------------------- LogMessageTime end ---------------------------- */


/* ----------------------------- LogSink ---------------------------- */
LogSink::~LogSink() = default;

void LogSink::send(LogSeverity severity, const char* full_filename, 
                    const char* base_filename, int line,
                    const LogMessageTime& logmsgtime, const char* message,
                    size_t message_len) {
  

  send(severity, full_filename, base_filename, line, &logmsgtime.tm(), message, message_len);
}

void LogSink::send(LogSeverity severity, const char* full_filename,
                    const char* base_filename, int line, const std::tm* t,
                    const char* message, size_t message_len) {
  
  // 默认实现(不做任何操作)
  (void)severity;
  (void)full_filename;
  (void)base_filename;
  (void)line;
  (void)t;
  (void)message;
  (void)message_len;
}

void LogSink::WaitTillSent() {
  // 默认不做操作
}

std::string LogSink::ToString(LogSeverity severity, const char* file, int line,
                     const LogMessageTime &logmsgtime,
                     const char* message, size_t message_len) {
  
  std::ostringstream stream;
  stream.fill('0');

  // TODO: 根据 FLAGS 决定是否记录年
  stream << setw(4) << 1900 + logmsgtime.year() << "-" 
        << setw(2) << 1 + logmsgtime.month() << "-" 
        << setw(2) << logmsgtime.day() << ' '
        << setw(2) << logmsgtime.hour() << ':'
        << setw(2) << logmsgtime.min() << ':'
        << setw(2) << logmsgtime.sec() << "."
        << setw(6) << logmsgtime.usec() << " ["
        << file << ':' << line << "]["
        << LogSeverityNames[severity] << "]: ";
  
  (stream.write)(message, static_cast<std::streamsize>(message_len));
  return stream.str();
}

/* ----------------------------- LogSink end ---------------------------- */





