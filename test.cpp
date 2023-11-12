#include "logging.h"
#include <chrono>
#include <iostream>

int main(int argc, char const *argv[])
{
  InitLogging(argv[0]);
  SetLogDir("/home/lizy/lizy_log/");
  SetLogDestination(LOG_INFO, "testI");
  SetLogDestination(LOG_WARNING, "testW");
  SetLogDestination(LOG_ERROR, "testE");
  SetLogFilenameExtension(".log");

  int epi = 50000;
  auto start = std::chrono::system_clock::now();
  for (int i = 0; i < epi; i++) {
    LOG(INFO) << "hello log" << i;
  }
  auto end = std::chrono::system_clock::now();

  auto res = std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
  std::cout << "QPS: " <<  epi / res.count() << "msg/s" << std::endl;

  return 0;
}
