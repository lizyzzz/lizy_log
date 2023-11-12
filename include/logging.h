#ifndef LIZY_LOGGING_H_
#define LIZY_LOGGING_H_

#include <ostream>
#include <iomanip>
#include <ctime>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <assert.h>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <dirent.h>
#include "type.h"
#include "utilities.h"

// 宏定义
#define COMPACT_LIZY_LOG_INFO LogMessage(__FILE__, __LINE__)
#define LOG_TO_STRING_INFO(message) LogMessage(__FILE__, __LINE__, LOG_INFO, message)

#define COMPACT_LIZY_LOG_WARNING LogMessage(__FILE__, __LINE__, LOG_WARNING)
#define LOG_TO_STRING_WARNING(message) LogMessage(__FILE__, __LINE__, LOG_WARNING, message)

#define COMPACT_LIZY_LOG_ERROR LogMessage(__FILE__, __LINE__, LOG_ERROR)
#define LOG_TO_STRING_ERROR(message) LogMessage(__FILE__, __LINE__, LOG_ERROR, message)

#define COMPACT_LIZY_LOG_FATAL LogMessage(__FILE__, __LINE__, LOG_FATAL)
#define LOG_TO_STRING_FATAL(message) LogMessage(__FILE__, __LINE__, LOG_FATAL, message)

// 标准宏定义
#define LOG(severity) COMPACT_LIZY_LOG_ ## severity.stream()

// Log to string 相关宏定义
#define LOG_TO_STRING(severity, message) LOG_TO_STRING_ ## severity(static_cast<std::string*>(message)).stream()
#define LOG_STRING(severity, outvec) LOG_TO_STRING_ ## severity(static_cast<std::vector<std::string>*>(outvec)).stream()

// LogSink 相关宏定义
#define LOG_TO_SINK(sink, severity) LogMessage(__FILE__, __LINE__, LOG_ ## severity, \
                                                static_cast<LogSink*>(sink), true).stream()

#define LOG_TO_SINK_BUT_NOT_TO_LOGFILE(sink, severity)        \
            LogMessage(__FILE__, __LINE__, LOG_ ## severity, \
            static_cast<LogSink*>(sink), false).stream()

// LOG_IF 相关宏定义
// static_cast<void>(0) 解释了 (void) 0 的作用, 如果条件为 false, 则执行 static_cast<void>(0), (void) 0 两句语句
#define LOG_IF(severity, condition) \
        static_cast<void>(0),       \
        !(condition) ? (void) 0 : LogMessageVoidify() & LOG(severity)

// LOG_ASSERT 相关宏定义
#define LOG_ASSERT(condition) LOG_IF(FATAL, !(condition)) << "Assert failed: " #condition

// CHECK 接口
#define lizy_PREDICT_BRANCH_NOT_TAKEN(x) x 
#define CHECK(condition) \
        LOG_IF(FATAL, lizy_PREDICT_BRANCH_NOT_TAKEN(!(condition))) << "Check failed: " #condition " "

// 用于 CKECK 的类 
struct CheckOpString {
  CheckOpString(std::string* str): str_(str) {}

  // 类型转化重载
  operator bool() const {
    return lizy_PREDICT_BRANCH_NOT_TAKEN(str_ != NULL);
  }

  std::string* str_;
};

// 为了允许在类中声明但未定义的静态整数常量作为CHECK*宏的参数, 对整数类型进行了重载, 但并不鼓励这样做
template <class T>
inline const T&       GetReferenceableValue(const T&           t) { return t; }
inline char           GetReferenceableValue(char               t) { return t; }
inline unsigned char  GetReferenceableValue(unsigned char      t) { return t; }
inline signed char    GetReferenceableValue(signed char        t) { return t; }
inline short          GetReferenceableValue(short              t) { return t; }
inline unsigned short GetReferenceableValue(unsigned short     t) { return t; }
inline int            GetReferenceableValue(int                t) { return t; }
inline unsigned int   GetReferenceableValue(unsigned int       t) { return t; }
inline long           GetReferenceableValue(long               t) { return t; }
inline unsigned long GetReferenceableValue(unsigned long t) { return t; }
inline long long      GetReferenceableValue(long long          t) { return t; }
inline unsigned long long GetReferenceableValue(unsigned long long t) {
  return t;
}

#define CHECK_OP(op, val1, val2) CHECK((val1) op (val2))

// CHECK比较接口
#define CHECK_EQ(val1, val2) CHECK_OP(==, val1, val2)
#define CHECK_NE(val1, val2) CHECK_OP(!=, val1, val2)
#define CHECK_LE(val1, val2) CHECK_OP(<=, val1, val2)
#define CHECK_LT(val1, val2) CHECK_OP(< , val1, val2)
#define CHECK_GE(val1, val2) CHECK_OP(>=, val1, val2)
#define CHECK_GT(val1, val2) CHECK_OP(> , val1, val2)

// 先声明
namespace log_internal_namespace_ {
  struct CrashReason;
}

typedef void (*logging_fail_func_t)() __attribute__((noreturn));
void InstallFailureFunction(logging_fail_func_t fail_func);

struct LogMessageTime {
  LogMessageTime();
  LogMessageTime(std::tm t);
  LogMessageTime(std::time_t timestamp, WallTime now);

  const time_t& timestamp() const { return timestamp_; }
  const int& sec() const { return time_struct_.tm_sec; }
  const int32_t& usec() const { return usecs_; }
  const int& min() const { return time_struct_.tm_min; }
  const int& hour() const { return time_struct_.tm_hour; }
  const int& day() const { return time_struct_.tm_mday; }
  const int& month() const { return time_struct_.tm_mon; }
  const int& year() const { return time_struct_.tm_year; }
  const int& dayOfWeek() const { return time_struct_.tm_wday; }
  const int& dayInYear() const { return time_struct_.tm_yday; }
  const int& dst() const { return time_struct_.tm_isdst; } // 夏令时
  const long int& gmtoffset() const { return gmtoffset_; }
  const std::tm& tm() const { return time_struct_; }

  private:
    void init(const std::tm& t, std::time_t timestamp, WallTime now);
    std::tm time_struct_; // LogMessage 创建的时间
    time_t timestamp_;    // LogMessage 创建的时间(单位:s)
    int32_t usecs_;       // LogMessage 创建的时间(微妙部分)
    long int gmtoffset_;  // 当前时区与标准时间(GMT)之间的偏移量(单位:s)

    void CalcGmtOffset();
};

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

namespace base_logging {
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

}


// 日志消息类
class LogMessage {
  public:
    enum {
      // 传递 kNoLogPrefix 给 行号 参数会禁用日志消息前缀
      kNoLogPrefix = -1
    };

      // 内嵌类型
    class LogStream : public std::ostream {
    public:
      LogStream(char* buf, int len, int64 ctr)
        : std::ostream(NULL), 
          streambuf_(buf, len), 
          ctr_(ctr), 
          self_(this) {
        rdbuf(&streambuf_); // 改变底层的缓冲区
      }

      int64 ctr() const { return ctr_; }
      void set_ctr(int64 ctr) { ctr_ = ctr; }
      LogStream* self() const { return self_; }

      // std::streambuf 的方法
      size_t pcount() const { return streambuf_.pcount(); }
      char* pbase() const { return streambuf_.pbase(); }
      // 返回缓冲区的字符串首地址
      char* str() const { return pbase(); }
    private:
      LogStream(const LogStream&) = delete;            // delete copy constructor
      LogStream& operator=(const LogStream&) = delete; // delete operator=
      base_logging::LogStreamBuf streambuf_;  // 缓冲区
      int64 ctr_;  // TODO: Counter hack (for the LOG_EVERY_X() macro)
      LogStream* self_; // 用于一致性检查
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
    void RecordCrashReason(log_internal_namespace_::CrashReason* reason);

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

// 当前仅当 ostream 是 LogStream 时起作用
std::ostream& operator<<(std::ostream &os, const PRIVATE_Counter&);

class LogMessageVoidify {
 public:
  LogMessageVoidify() { }
  
  // 重载 & 返回空
  // 用于 LOG_IF
  // 二元运算符 & 优先级要大于“?:”，但是要小于“<<”。这 “&” 二元运算刚好满足
  void operator&(std::ostream&) {}
};


namespace base {

// Logger 是日志模块用来将日志条目输出到日志的接口
// 是抽象基类, 需要继承实现
class Logger {
 public:
  virtual ~Logger();

  // 将对应于发生在"时间戳"处的事件的消息"message[0,message_len-1]"写入日志文件
  // 如果“force_flush”为true，则立即刷新日志文件
  // 输入的消息已经由更高级别的日志记录设施进行了适当的格式化。例如，文本日志消息已经包含了时间戳以及文件名:行号的前缀
  // 子类重写这个函数可以实现异步写
  virtual void Write(bool force_flush, time_t timestamp, const char* message, size_t message_len) = 0;

  // 刷盘所有的信息
  virtual void Flush() = 0;

  // 返回日志文件的大小, 返回的值可能是近似值, 因为一些日志可能还没刷新到磁盘上
  virtual uint32 LogSize() = 0;

};

// 获取指定严重程度级别的日志记录器
// 日志记录器仍然属于日志模块的所有权, 不应由调用者删除
// 线程安全的
Logger* GetLogger(LogSeverity level);

// 设置指定严重程度级别的日志记录器
// 日志记录器将成为日志模块的所有权, 调用者不应该删除它
// 线程安全的
void SetLogger(LogSeverity level, Logger* logger);

} // end of namespace base


// 一些接口函数 //

// 初始化日志库
void InitLogging(const char* argv0);

// 检查是否已经初始化
bool IsLoggingInitialized();

// 停止日志库
void ShutdownLogging();

// 启动或停止过期日志清理功能
void EnableLogCleaner(unsigned int overdue_days);
void DisableLogCleaner();

// FLAGS 设置接口

// 日志直接输出到 stderr
void SetLogtostderr(bool flag);
// 日志直接输出到 stdout
void SetLogtostdout(bool flag);
// 特定程度的日志输出到文件的同时是否也输出到 stderr
void SetAlsologtostderr(bool flag);
// 输出到 stderr 是否带颜色
void SetColorlogtostderr(bool flag);
// 输出到 stdout 是否带颜色
void SetColorlogtostdout(bool flag);
// 在磁盘满时是否继续写
void SetStopLoggingIfFullDisk(bool flag);
// 是否记录标准时间
void SetLogUTCtime(bool flag);
// 是否在 logfile 的名字中记录时间和pid
void SetTimestampInLogfileName(bool flag);
// 是否记录头部
void SetLogFileHeader(bool flag);
// 是否在日志前缀记录年
void SetLogYearInPrefix(bool flag);
// 是否定时清理一些日志文件在内存中的缓存
void SetDropLogMemory(bool flag);

// 写到 stderr 的日志程度阈值
void SetStderrThreshold(int level);
// 日志记录的最小等级
void SetMinLogLevel(int level);
// 日志可以异步刷盘的最高等级
void SetLogBufLevel(int level);
// 日志刷盘的最长时间间隔(单位: s)
void SetLogBufSecs(int seconds);
// 日志文件的权限
void SetLogfileMode(int mode);
// 检查是否有需要过期的日志需要清理的时间间隔
void SetLogcleanSecs(int seconds);
// 日志文件的目的文件夹
void SetLogDir(const std::string& dir);
// 日志文件软链接的文件夹
void SetLogLink(const std::string& link);

// 日志文件最大的大小
void SetMaxLogSize(uint32 size);


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

// TODO: 增加日志截断
// 截断一个可能是只允许多个进程追加模式输出的日志文件(通常是stdout/stderr)
// 因此不能简单地重命名/重新打开
// 如果文件 "path" 超过了 "limit" 字节，将最后的 "keep" 字节复制到偏移量0并截断其余部分。
// 由于可能会与其他写入者竞争，这种方法有可能会丢失极小量的数据。出于安全考虑，仅在路径为 /proc/self/fd/* 时才跟随符号链接。
// void TruncateLogFile(const char* path, uint64 limit, uint64 keep);

// 如果 stdout 和 stderr 超过指定的值, 则截断至 max_log_size,
// 保留到最后的 1MB, 这个函数与 TruncateLogFile 函数一样存在竞态
// void TruncateStdoutStderr();



#endif

