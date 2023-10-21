#include "./include/lizylog.h"
#include "iostream"


int main(int argc, char const *argv[])
{
  std::cout << "cout hello log" << std::endl;

  LOG(WARNING) << "lizy";
  LOG(INFO) << "hello log";

  return 0;
}
