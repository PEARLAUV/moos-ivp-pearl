/************************************************************/
/*    FILE: main.cpp                                        */
/************************************************************/

#include <cstdlib>
#include <iostream>
#include <string>

#include "ColorParse.h"
#include "MBUtils.h"
#include "MavlinkLandingTarget.h"

namespace
{
void showHelpAndExit()
{
  std::cout << "Usage: pMavlinkLandingTarget file.moos [OPTIONS]\n"
            << "  --alias=NAME  Run with the given MOOS process name\n"
            << "  --help, -h    Show this help\n";
  std::exit(0);
}
} // namespace

int main(int argc, char *argv[])
{
  std::string mission_file;
  std::string run_command = argv[0];

  for(int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if(argument == "-h" || argument == "--help" || argument == "-help")
      showHelpAndExit();
    if(strEnds(argument, ".moos") || strEnds(argument, ".moos++"))
      mission_file = argument;
    else if(strBegins(argument, "--alias="))
      run_command = argument.substr(8);
    else if(i == 2)
      run_command = argument;
  }

  if(mission_file.empty())
    showHelpAndExit();

  std::cout << termColor("green") << "pMavlinkLandingTarget launching as "
            << run_command << termColor() << std::endl;

  MavlinkLandingTarget app;
  return app.Run(run_command.c_str(), mission_file.c_str()) ? 0 : 1;
}
