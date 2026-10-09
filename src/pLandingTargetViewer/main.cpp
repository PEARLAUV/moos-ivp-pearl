#include "LandingTargetViewer.h"
#include "MBUtils.h"
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
  std::string mission, name = "pLandingTargetViewer";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: pLandingTargetViewer file.moos [--alias=NAME]\n";
      return 0;
    }
    if (strEnds(arg, ".moos") || strEnds(arg, ".moos++"))
      mission = arg;
    else if (strBegins(arg, "--alias="))
      name = arg.substr(8);
    else if (i == 2)
      name = arg;
  }
  if (mission.empty()) {
    std::cerr << "A mission file is required; use --help.\n";
    return 1;
  }
  LandingTargetViewer app;
  return app.Run(name.c_str(), mission.c_str()) ? 0 : 1;
}
