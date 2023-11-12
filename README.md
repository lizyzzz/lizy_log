# lizy_log

基于glog框架的可扩展高性能C++日志组件  
类图 UML:
[UML](https://github.com/lizyzzz/lizy_log/blob/main/images/Log%20%E7%B1%BB%E5%9B%BE.jpg)

## 安装与使用

```bash
mkdir build
cd build
cmake ..
make 
sudo make install
```

* 默认的安装路径是 `/usr/local/lib`, 所以您需要把该路径添加到动态链接库路径中

```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
// 或者把这句命令添加到您终端的配置文件中
```

* 使用时

```cpp
#include "logging.h"


int main(int argc, char const *argv[])
{
  InitGoogleLogging(argv[0]);
  SetLogDestination(GLOG_INFO, "testLog");
  SetLogFilenameExtension(".log");

  LogSink s;
  LOG_TO_SINK(&s, WARNING) << "sink";

  string str;
  LOG_TO_STRING(INFO, &str) << "log to string";
  std::vector<string> vec;
  LOG_STRING(INFO, &vec) << "log to vec";

  LOG_IF(INFO, false) << "log if";


  LOG(WARNING) << "lizy";
  for (int i = 0; i < 100; i++) {
    LOG(INFO) << "hello log";
  }

  int a = 1;
  int b = 2;
  CHECK(a == b);

  return 0;
}
```

## 0. 实现的功能

* 实现像 std::cout 那样的输出方式输出日志

* 默认可以选择把日志输出到 标准输出 或 标准错误 或 日志文件

* 采用异步的方式把日志落地

* 提供容易扩展的接口可以把日志落地到远程服务器或自定义目的地

```cpp
// TODO list:
/*
  (1) 自定义 Logger 或 LogSink 适应不同的场景
*/
```
* 测试  
![QPS](https://github.com/lizyzzz/lizy_log/blob/main/images/QPS.png)

## 1. 日志库最基本的特性  

* 日志信息，自定义日志输出信息，方便获取程序上下文的信息，比如变量；
  
```cpp
  snprintf(buf, len, "%s", log_msg);
```

* 日志发生的时间，主要是为了方便对应程序中某个事件发生的时间；

```cpp
  snprintf(buf, len, "%s %s", t.toyymmddHHmmss(), log_msg);
```

* 日志发生的位置，主要是为了方便对应程序中哪部分代码触发了日志；

```cpp
  snprintf(buf, len, "%s %s:%d %s", t.toyymmddHHmmss(), __FILE__, __LINE__, log_msg);
```

* 日志过滤，有时候希望测试环境打印多一点日志，但是生产环境尽量少打日志；

```cpp
  level < LEVEL_ERROR? (void)0:
  snprintf(buf, len, "%s %s:%d %s", t.toyymmddHHmmss(), __FILE__, __LINE__, log_msg);
```

* 日志写入本地磁盘，或者传输到远程服务器。

我们常常需要先将日志信息记录到 log_msg、然后进行过滤，经过过滤下沉的日志再进行格式化，最后输出到文件或者远程服务。用术语说的话即是：记录器 ->过滤器 -> 格式化器 -> 输出器。根据实现方式的不一样，过滤器可能会在记录器前面，看具体的实现方式。  

## 2. 日志接口

```cpp
  #define COMPACT_LIZY_LOG_INFO LogMessage(__FILE__, __LINE__)

  #define COMPACT_LIZY_LOG_WARNING LogMessage(__FILE__, __LINE__, GLOG_WARNING)

  #define COMPACT_LIZY_LOG_ERROR LogMessage(__FILE__, __LINE__, GLOG_ERROR)

  #define COMPACT_LIZY_LOG_FATAL LogMessage(__FILE__, __LINE__, GLOG_FATAL)

  #define LOG(severity) COMPACT_LIZY_LOG_ ## severity.stream()
```

* LOG：输出到默认日志输出目标

```cpp
  // 常用方式
  LOG(INFO) << "message";
  LOG(WARNING) << "lizy";

  // 输出到指定的目的地
  LogSink s;
  LOG_TO_SINK(&s, WARNING) << "sink";
  // 输出到 string
  string str;
  LOG_TO_STRING(INFO, &str) << "log to string";
  // 输出到 vector<string>
  std::vector<string> vec;
  LOG_STRING(INFO, &vec) << "log to vec";
  // LOG_IF
  LOG_IF(INFO, false) << "log if";
  // CHECK
  int a = 1;
  int b = 2;
  CHECK(a == b);
```

### 2.1 日志使用

```cpp
// 初始化日志库
void InitGoogleLogging(const char* argv0);

// 检查是否已经初始化
bool IsGoogleLoggingInitialized();

// 停止日志库
void ShutdownGoogleLogging();

// 启动或停止过期日志清理功能
void EnableLogCleaner(unsigned int overdue_days);
void DisableLogCleaner();


// 设置 FALTAL 时执行的函数
void InstallFailureFunction(logging_fail_func_t fail_func);

// 获取日志等级对应的名字
const char* GetLogSeverityName(LogSeverity severity);

// 获取日志文件目录
const std::vector<std::string>& GetLoggingDirectories();

// 返回已存在的临时目录, 将会是 GetLoggingDirectories() 的子集
void GetExistingTempDirectories(std::vector<std::string>* list);

// 允许再次打印任意 fatal 信息
void ReprintFatalMessage();


// 刷盘所有包含不低于指定等级日志的日志文件(线程安全) 
void FlushLogFiles(LogSeverity min_severity);

// 刷盘所有包含不低于指定等级日志的日志文件(线程不安全) 
void FlushLogFilesUnsafe(LogSeverity min_severity);

// 设置特定日志等级的目的文件名, 如果为"" ,则表示不记录该日志(线程安全)
void SetLogDestination(LogSeverity severity, const char* base_filename);

// 设置特定严重程度级别的最新日志文件的符号链接的基本名称
// 如果 symlink_basename 为空，则不创建符号链接
// 如果不调用此函数, 则符号链接的基本名称将是程序的调用名称(线程安全)
void SetLogSymlink(LogSeverity severity, const char* symlink_basename);


// 增加或者移除 LogSink 作为下沉对象(线程安全)
void AddLogSink(LogSink* destination);
void RemoveLogSink(LogSink* destination);

// 指定通过 SetLogDestination 增加的文件名的后缀名
// 这适用于所有严重等级
void SetLogFilenameExtension(const char* filename_extension);

// 使得所有至少具有特定严重程度级别的日志消息会被记录到标出错误输出(线程安全)
void SetStderrLogging(LogSeverity min_serverity);

// 使所有日志消息只被发送到标准错误输出
void LogToStderr();

namespace base {

// 获取指定严重程度级别的日志记录器
// 日志记录器仍然属于日志模块的所有权, 不应由调用者删除
// 线程安全的
Logger* GetLogger(LogSeverity level);

// 设置指定严重程度级别的日志记录器
// 日志记录器将成为日志模块的所有权, 调用者不应该删除它
// 线程安全的
void SetLogger(LogSeverity level, Logger* logger);

} // end of namespace base

```

## 3. 实现原理

### 3.1 日志过滤

待实现接口

### 3.2 日志记录

glog 的日志记录其实是通过 LogMessage 类来实现的，打印日志的时候构造一个 LogMessage 临时对象，同时进行初始化，包括写入格式化后的日志前缀，然后通过流输出的方式将日志内存记录到 buffer 中，在 LogMessage 临时对象析构的时候进行落地。

```cpp
// 日志消息类
class LogMessage {
  public:
    enum {
      // 传递 kNoLogPrefix 给 行号 参数会禁用日志消息前缀
      kNoLogPrefix = -1
    };

    // 内嵌类型(流的方式)
    class LogStream : public std::ostream {
    public:
      // ...
    };

  public:
    typedef void (LogMessage::*SendMethod)(); // 成员函数指针类型
    
    LogMessage(const char* file, int line, LogSeverity severity, int64 ctr, SendMethod send_method);
    // 两个特殊的构造函数, 在常见情况下 LOG 的调用位置减少生成的代码量
    // 用于 LOG(INFO): 隐含的是: severity = INFO, ctr = 0, send_method = &LogMessage::SendToLog
    // 使用这个构造函数而不上面那个复杂的构造函数可以节省19个字节.
    LogMessage(const char* file, int line);

    // 用于 LOG(severity) 其中 severity != INFO
    // 隐含的是: ctr = 0, send_method = &LogMessage::SendToLog
    // 使用这个构造函数而不上面那个复杂的构造函数可以节省17个字节.
    LogMessage(const char* file, int line, LogSeverity severity);
    
    // 用于把日志记录到 sink 中
    // 隐含的是: ctr = 0, send_method = also_send_to_log ? &LogMessage::SendToSinkAndLog : &LogMessage::SendToSink
    LogMessage(const char* file, int line, LogSeverity severity, LogSink* sink, bool also_send_to_log);

    // 用于把日志保存在 std::vector<std::string> 中
    // 隐含的是: ctr = 0, send_method = &LogMessage::SaveOrSendToLog
    LogMessage(const char* file, int line, LogSeverity severity, std::vector<std::string>* outvec);

    // 用于把日志保存在 std::string 中
    // 隐含的是: ctr = 0, send_method = &LogMessage::WriteToStringAndLog
    LogMessage(const char* file, int line, LogSeverity severity, std::string* message);

    // 特殊的构造函数 用于 check 失败时
    LogMessage(const char* file, int line, const CheckOpString& result);

    ~LogMessage();

    // 将所有日志消息刷盘到 sink 对象, 总是在析构函数调用, 也可以在任何地方调用如果有需要同步日志的时候
    void Flush();

    // 单条日志最长长度
    static const size_t kMaxLogMessageLen;

    // 这些函数不应该在其他地方(非logging.*)直接调用, 应该作为参数 SendMethod 传递
    void SendToLog();
    void SendToSyslogAndLog();

    // FATAL 错误结束进程函数
    [[noreturn]] static void Fail();

    // 返回一个流
    std::ostream& stream();

    int preserved_errno() const;

    // 调用时不能持有锁
    static int64 num_messages(int severity);

    const LogMessageTime& getLogMessageTime() const;

    struct LogMessageData;

  private:

    // 所有的 SendMethod 方法
    void SendToSinkAndLog();
    void SendToSink();

    void WriteToStringAndLog();

    void SaveOrSendToLog();

    // 构造函数调用的初始化函数
    void Init(const char* file, int line, LogSeverity severity, void (LogMessage::*send_method)());

    // 用于记录错误原因当 FATAL 发生时
    void RecordCrashReason(glog_internal_namespace_::CrashReason* reason);

    // 每个优先级发送的消息计数
    static int64 num_messages_[NUM_SEVERITIES];

    // 将 data 保存在单独的结构体中是为了每个 LogMessage 实例使用更少的栈空间
    LogMessageData* allocated_; // 内存分配
    LogMessageData* data_;      // 内存分配的拷贝
    LogMessageTime logmsgtime_;

    friend class LogDestination;

    LogMessage(const LogMessage&) = delete; // delete copy constructor
    LogMessage& operator=(const LogMessage&) = delete; // delete operator=
};
```

LogMessage 对象的构造函数比较多, 但都是为了实现不同的宏接口，以最 LOG(INFO) 为例，Init 函数负责构造 LogMessageData 对象，
其中比较重要的是初始化 send_method，该成员变量是一个成员函数指针，会在 LogMessage 的成员函数 Flush() 中被调用。比如 INFO 水平下，
send_method = &LogMessage::SendToLog()， SendToLog() 依赖于 LogDestination 中的静态成员函数，调用这些静态成员函数对日志进行落地。
并且组织日志头部格式化。头部格式化内容包括：时间，文件，行号，级别等。

```cpp
LogMessage::LogMessage(const char* file, int line) : allocated_(nullptr) {
  Init(file, line, GLOG_INFO, &LogMessage::SendToLog);
}
```

而 LogMessageData 对象包含带有缓冲区的流对象，流对象继承自 std::ostream，流对象中的缓冲区继承自 std::streambuf
在 LogMessageData() 构造函数中对流对象初始化

```cpp
struct LogMessage::LogMessageData {
  LogMessageData();
  // ...
  // 缓冲区空间
  char message_text_[kMaxLogMessageLen+1];
  LogStream stream_;
  char severity_;
  int line_;
  void (LogMessage::*send_method_)(); // 在析构函数中调用
  // ...
};

LogMessage::LogMessageData::LogMessageData()
  : stream_(message_text_, LogMessage::kMaxLogMessageLen, 0) { 
  // 初始化 LogStream
}

class LogStream : public std::ostream {
public:
  LogStream(char* buf, int len, int64 ctr)
    : std::ostream(NULL), 
      streambuf_(buf, len), // 初始化 LogStreamBuf
      ctr_(ctr), 
      self_(this) {
    rdbuf(&streambuf_); // 把 streambuf_ 作为 LogStream 对象的底层缓冲区
  }
  // ...
  LogStream* self() const { return self_; }

  // std::streambuf 的方法
  size_t pcount() const { return streambuf_.pcount(); }
  char* pbase() const { return streambuf_.pbase(); }
  // 返回缓冲区的字符串首地址
  char* str() const { return pbase(); }
private:
  base_logging::LogStreamBuf streambuf_;  // 缓冲区
  int64 ctr_;  // TODO: Counter hack (for the LOG_EVERY_X() macro)
  LogStream* self_; // 用于一致性检查
};

// LogStreamBuf 继承 std::streambuf
// std::streambuf 是输入输出操作的基础组件, std::istream 和 std::ostream 都有一个 std::streambuf 指针
// LogStreamBuf 忽略溢出且最后两个字符是 '\n' '\0'
class LogStreamBuf : public std::streambuf {
  public:
    LogStreamBuf(char* buf, int len) {
      setp(buf, buf + len - 2); // 设置缓冲区的起始位置
    }

    // 负责将缓冲区中的数据写入到输流中。当缓冲区已满时，或者在需要强制刷新缓冲区时，这个函数会被调用。
    // 用于输出操作, 缓冲区满时直接输出(即忽略溢出)
    // override: 由 I/O 自动调用
    int_type overflow(int_type ch) {
      return ch;
    }

    // 公共 ostream 方法
    // 返回缓冲区填充的长度
    size_t pcount() const { return static_cast<size_t>(pptr() - pbase()); }
    // 返回首地址
    char* pbase() const { return std::streambuf::pbase(); }
};

```

### 3.3 日志格式化

日志格式化是在 LogMessage 对象初始化的时候做的，也是在 LogMessage 构造的时候，在调用 Init 函数时

```cpp
void LogMessage::Init(const char* file, int line, LogSeverity severity, void (LogMessage::*send_method)()) {
  // ...

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
```

### 3.4 日志输出

日志的输出在 LogMessage 临时对象析构的时候。以 LOG(INFO) 为例

```cpp
void LogMessage::Init(const char* file, int line, LogSeverity severity, void (LogMessage::*send_method)()) {
  // ...
  // 构造函数时初始化 send_method, LOG(INFO) 时为 SendToLog()
  data_->send_method_ = send_method;
  // ...
}

// 析构函数时刷盘
LogMessage::~LogMessage() {
  Flush();
  delete allocated_;
}

void LogMessage::Flush() {
  // ...
  {
    std::lock_guard<std::mutex> lk(log_mutex);
    (this->*(data_->send_method_))(); // 执行回调函数(日志发送下一步处理)
    ++num_messages_[static_cast<int>(data_->severity_)];
  }
  LogDestination::WaitForSinks(data_);
  // ...
}

// 必须持有 log_mutex
void LogMessage::SendToLog() {
  // ...
  // 日志落地
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
}

// 而 LogToAllLogfiles 调用 MaybeLogToLogfile 最终进一步调用 logger 的 write 方法实现落地
void LogDestination::MaybeLogToLogfile(LogSeverity severity, time_t timestamp, const char* message, size_t len) {
  const bool should_flush = severity > FLAGS_logbuflevel;
  LogDestination* destination = log_destination(severity);
  destination->logger_->Write(should_flush, timestamp, message, len); // 日志落地
}

// 默认的日志落地文件方式由 LogFileObject 实现
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

```

### 3.5 LogSink 扩展

整个实现中有不足的地方： 

* 1. 日志输出，为了多线程安全，在写入的时候有加锁，这在大量日志的场景（比如 monitor 日志的场景）会影响性能； 

* 2. 默认日志输出，ERROR 日志会同时输出到3个日志文件（XXX.INFO、XXX.WARNING、XXX.ERROR），有些时候我们其实不需要输出到多个文件，只需一个文件即可； 

* 3. 有时候我们会定义不同的日志文件（比如 XXX.app、XXX.monitor、XXX.sys 等），分别作为不同日志的输出，但 LOG(serverity) 不支持

#### 3.5.1 继承 LogSink 接口，并注册接口

```cpp
// sink 扩展类 ( 基类 )
class LogSink {
public:
  virtual ~LogSink();
  // message_len 是为了排除末尾的 '\n'
  // 此方法内不能调用 LOG() 或者 CHECK() 因为调用时已经拿到互斥锁
  virtual void send(LogSeverity severity, const char* full_filename, 
                    const char* base_filename, int line,
                    const LogMessageTime& logmsgtime, const char* message,
                    size_t message_len);
  // 重载(兼容时间)
  virtual void send(LogSeverity severity, const char* full_filename,
                    const char* base_filename, int line, const std::tm* t,
                    const char* message, size_t message_len);

  // 这个函数用于实现等待日志输出完成的逻辑. 它会在每次 send() 函数返回后, 且在 LogMessage 退出或崩溃之前执行 被调用.
  // 默认情况下, 这个函数不执行任何操作
  // 使用这个函数可以为 send() 实现复杂的逻辑, 甚至 send() 包含日志输出
  // 这种情况下, 如果日志输出逻辑需要在不同的线程中执行, 那么 send() 方法可以将消息添加到一个队列中, 
  // 然后唤醒另一个处理实际日志输出的线程, 同时自身可以进行一些 LOG() 调用。
  // WaitTillSent() 的具体实现可以用来等待这个逻辑完成.
  virtual void WaitTillSent();

  // 返回日志消息的字符串
  // 对 send() 的实现很有用
  static std::string ToString(LogSeverity severity, const char* file, int line,
                              const LogMessageTime &logmsgtime,
                              const char* message, size_t message_len);
};
```

### 3.6 总结

* `LogMessage`：日志记录器的构造和初始化，同时还做了日志前缀格式化；

* `LogStream`：让日志接口可以向 std::cout 那样流方式的输出；

* `LogDestination`：日志落地目标的封装，包含的 Logger 成员和 vector<LogSink*>；

* `LogFileObject`：实现了默认的文件方式的日志落地；

* `LogSink`：因为日志库本身实现的是轻量的日志落地方式，但是也保留了sink接口供开发者扩展。
