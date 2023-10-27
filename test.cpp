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
