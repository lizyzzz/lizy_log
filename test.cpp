#include "logging.h"


int main(int argc, char const *argv[])
{
  InitLogging(argv[0]);
  SetLogDir("/home/lizy/lizy_log/");
  SetLogDestination(LOG_INFO, "test");
  SetLogFilenameExtension(".log");

  LogSink s;
  LOG_TO_SINK(&s, WARNING) << "sink";

  std::string str;
  LOG_TO_STRING(INFO, &str) << "log to string";
  std::vector<std::string> vec;
  LOG_STRING(INFO, &vec) << "log to vec";

  LOG_IF(INFO, false) << "log if";

  int num = 100;
  LOG(WARNING) << "lizy" << num;
  LOG(ERROR) << "lizy" << num + 1;
  for (int i = 0; i < 100; i++) {
    LOG(INFO) << "hello log";
  }

  int a = 1;
  int b = 2;
  // CHECK(a == b);

  return 0;
}
