#include "./include/logging.h"
#include "iostream"


int main(int argc, char const *argv[])
{
  InitGoogleLogging(argv[0]);
  SetLogDestination(GLOG_INFO, "testLog");
  SetLogFilenameExtension(".log");

  std::cout << "cout hello log" << std::endl;

  LOG(WARNING) << "lizy";
  for (int i = 0; i < 100; i++) {
    LOG(INFO) << "hello log";
  }

  return 0;
}
