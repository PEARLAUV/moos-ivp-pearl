#include "MBUtils.h"
#include "MediatorRouter.h"
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
  std::string mission, alias = "pMediatorRouter";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "pMediatorRouter mission.moos [--alias=NAME]\n"
                   "Explicit mediated message/ACK destination dispatcher.\n";
      return 0;
    }
    if (strEnds(arg, ".moos") || strEnds(arg, ".moos++"))
      mission = arg;
    else if (strBegins(arg, "--alias="))
      alias = arg.substr(8);
    else if (i == 2)
      alias = arg;
  }
  if (mission.empty())
    return 1;
  MediatorRouter app;
  app.Run(alias.c_str(), mission.c_str());
  return 0;
}
