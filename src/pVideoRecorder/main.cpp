#include <string>

#include "MBUtils.h"
#include "VideoRecorder.h"

using namespace std;

int main(int argc, char *argv[])
{
  string mission_file;
  string run_command = argv[0];

  for (int i = 1; i < argc; ++i) {
    const string argument = argv[i];
    if (strEnds(argument, ".moos") || strEnds(argument, ".moos++"))
      mission_file = argument;
    else if (strBegins(argument, "--alias="))
      run_command = argument.substr(8);
    else if (i == 2)
      run_command = argument;
  }

  if (mission_file.empty())
    return 1;

  VideoRecorder app;
  app.Run(run_command.c_str(), mission_file.c_str());
  return 0;
}
