#include "MBUtils.h"
#include "RallyPoint.h"
#include <iostream>

int main(int argc, char **argv)
{
  std::string mission, name = "pRallyPoint";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: pRallyPoint file.moos [--alias=NAME]\n";
      return 0;
    }
    if (strEnds(arg, ".moos"))
      mission = arg;
    else if (strBegins(arg, "--alias="))
      name = arg.substr(8);
    else if (i == 2)
      name = arg;
  }
  if (mission.empty())
    return 1;
  RallyPoint app;
  return app.Run(name.c_str(), mission.c_str()) ? 0 : 1;
}
