#include "Operator.h"

#include <string>

int main(int argc, char *argv[]) {
  std::string mission_file;
  std::string app_name = "pOperator";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.size() >= 5 && (arg.substr(arg.size() - 5) == ".moos" ||
                            (arg.size() >= 7 && arg.substr(arg.size() - 7) == ".moos++")))
      mission_file = arg;
    else if (arg.compare(0, 8, "--alias=") == 0)
      app_name = arg.substr(8);
    else if (i == 2)
      app_name = arg;
  }
  if (mission_file.empty()) return 1;
  Operator app;
  app.Run(app_name.c_str(), mission_file.c_str());
  return 0;
}
