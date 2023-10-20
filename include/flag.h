#include <string>
#include "type.h"
using std::string;

// TODO: read option from Json

bool FLAGS_logtostderr = false;
bool FLAGS_alsologtostderr = false;
bool FLAGS_colorlogtostderr = false;
bool FLAGS_colorlogtostdout = false;
bool FLAGS_logtostdout = false;
bool FLAGS_stop_logging_if_full_disk = false;
bool FLAGS_log_utc_time = false;
bool FLAGS_timestamp_in_logfile_name = true;
bool FLAGS_log_file_header = true;
bool FLAGS_log_year_in_prefix = true;
bool FLAGS_drop_log_memory = true;

int32 FLAGS_stderrthreshold = GLOG_ERROR;
int32 FLAGS_minloglevel = 0;
int32 FLAGS_logbuflevel = 0;
int32 FLAGS_logbufsecs = 30;
int32 FLAGS_logfile_mode = 0664;
int32 FLAGS_logcleansecs = 60 * 5; // 5 min

string FLAGS_log_dir = "./";
string FLAGS_log_link = "";

uint32 FLAGS_max_log_size = 1800; // "MB"


// int32 