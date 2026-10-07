#include <string>
#include "MBUtils.h"
#include "TakeoffProcessor.h"
using namespace std;
int main(int argc, char* argv[])
{
  string mission, alias = argv[0];
  for(int i=1; i<argc; ++i) {
    const string arg = argv[i];
    if(strEnds(arg, ".moos") || strEnds(arg, ".moos++")) mission = arg;
    else if(strBegins(arg, "--alias=")) alias = arg.substr(8);
    else if(i == 2) alias = arg;
  }
  if(mission.empty()) return 1;
  TakeoffProcessor app;
  app.Run(alias.c_str(), mission.c_str());
  return 0;
}
