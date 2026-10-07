#include <string>

#include "MBUtils.h"
#include "uChecks.h"

using namespace std;

int main(int argc, char* argv[])
{
  string mission_file;
  string alias = argv[0];
  for(int i=1; i<argc; ++i) {
    const string arg = argv[i];
    if(strEnds(arg, ".moos") || strEnds(arg, ".moos++"))
      mission_file = arg;
    else if(strBegins(arg, "--alias="))
      alias = arg.substr(8);
    else if(i == 2)
      alias = arg;
  }
  if(mission_file.empty())
    return 1;
  uChecks app;
  app.Run(alias.c_str(), mission_file.c_str());
  return 0;
}
