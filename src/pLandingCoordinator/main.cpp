#include <string>
#include "MBUtils.h"
#include "ColorParse.h"
#include "LandingCoordinator.h"

using namespace std;

int main(int argc, char *argv[])
{
  string mission_file;
  string run_command=argv[0];
  for(int i=1;i<argc;i++) {
    string arg=argv[i];
    if(strEnds(arg,".moos")||strEnds(arg,".moos++")) mission_file=arg;
    else if(strBegins(arg,"--alias=")) run_command=arg.substr(8);
    else if(i==2) run_command=arg;
  }
  if(mission_file.empty()) { cout<<"Usage: pLandingCoordinator mission.moos\n"; return 1; }
  LandingCoordinator app;
  app.Run(run_command.c_str(),mission_file.c_str());
  return 0;
}
