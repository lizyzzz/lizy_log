#ifndef LIZY_FLAG_H_
#define LIZY_FLAG_H_
#pragma once

#include <string>
#include "type.h"
using std::string;

// TODO: read option from Json

// 日志直接输出到 stderr
bool FLAGS_logtostderr = false;
// 日志直接输出到 stdout
bool FLAGS_logtostdout = false;
// 特定程度的日志输出到文件的同时是否也输出到 stderr
bool FLAGS_alsologtostderr = false;
// 输出到 stderr 是否带颜色
bool FLAGS_colorlogtostderr = true;
// 输出到 stdout 是否带颜色
bool FLAGS_colorlogtostdout = true;
// 在磁盘满时是否继续写
bool FLAGS_stop_logging_if_full_disk = false;
// 是否记录标准时间
bool FLAGS_log_utc_time = false;
// 是否在 logfile 的名字中记录时间和pid
bool FLAGS_timestamp_in_logfile_name = true;
// 是否记录头部
bool FLAGS_log_file_header = true;
// 是否在日志前缀记录年
bool FLAGS_log_year_in_prefix = true;
// 是否定时清理一些日志文件在内存中的缓存
bool FLAGS_drop_log_memory = true;

// 写到 stderr 的日志程度阈值
int32 FLAGS_stderrthreshold = LOG_ERROR;
// 日志记录的最小等级
int32 FLAGS_minloglevel = LOG_INFO;
// 日志可以异步刷盘的最高等级
int32 FLAGS_logbuflevel = LOG_INFO;
// 日志刷盘的最长时间间隔(单位: s)
int32 FLAGS_logbufsecs = 30;
// 日志文件的权限
int32 FLAGS_logfile_mode = 0664;
// 检查是否有需要过期的日志需要清理的时间间隔
int32 FLAGS_logcleansecs = 60 * 5; // 5 min

// 日志文件的目的文件夹
string FLAGS_log_dir = "./";
// 日志文件软链接的文件夹
string FLAGS_log_link = "";

// 日志文件最大的大小
uint32 FLAGS_max_log_size = 1000; // "MB"


// int32 

#endif