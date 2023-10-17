#include "logging.h"

using std::setw;

const size_t LogMessage::kMaxLogMessageLen = 30000;


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

namespace {
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
    uint32 LogSize() override;

    // 内部的刷盘接口, 暴露此接口是为了 FlushLogFilesUnsafe() 可以在不获取锁的情况下调用这个接口
    // 通常 Flush() 在获取锁后才调用这个接口
    void FlushUnlocked();

   private:
    static const uint32 kRolloverAttemptFrequency = 0x20; // 日志滚动频率

    std::mutex lock_;
    bool base_filename_selected_;
    std::string base_filename_;
    std::string symlink_basename_;
    std::string filename_extension_;
    FILE* file{nullptr};
    LogSeverity severity_;
    uint32 bytes_since_flush_{0};
    uint32 dropped_mem_length_{0};
    uint32 file_length_{0};
    unsigned int rollover_attempt_; // 日志滚动次数(即另外新建一个新的日志文件)
    int64 next_flush_time_{0};      // 经过多少个周期后进行日志刷盘操作
    WallTime start_time_;

    // 根据文件名和可选参数time_pid_string创建日志文件
    // 要求: 必须持有锁
    bool CreateLogfile(const std::string& time_pid_string);
  };

  // TODO: lastest update
  class LogCleaner {
    
  };
}

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

/* ------------------------------------------------------------------------------ */


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


/* ------------------------------------------------------------------------------ */

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

// TODO: lastest update
void LogMessage::SendToLog() {
  
}

// mutex
static std::mutex log_mutex;

// 每种优先级被发送的信息的数量
int64 LogMessage::num_messages_[NUM_SEVERITIES] = {0, 0, 0, 0};

// 禁止继续记录日志的标记 (当磁盘满时)
static bool stop_writing = false;

const char* const LogSeverityNames[NUM_SEVERITIES] = {
  "INFO", "WARNING", "ERROR", "FATAL"
};

const char* GetLogSeverityName(LogSeverity severity) {
  return LogSeverityNames[severity];
}

