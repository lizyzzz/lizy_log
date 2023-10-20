#include "./include/lizylog.h"
#include "iostream"


int main(int argc, char const *argv[])
{
  std::cout << "hello log" << std::endl;

  LOG(INFO) << "lizy";

  return 0;
}
