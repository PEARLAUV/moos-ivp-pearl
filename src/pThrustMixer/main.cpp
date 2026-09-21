/************************************************************/
/*    NAME: PEARL Project                                   */
/*    FILE: main.cpp                                        */
/************************************************************/

#include <iostream>
#include <string>
#include "ColorParse.h"
#include "MBUtils.h"
#include "ThrustMixer.h"
#include "ThrustMixer_Info.h"

using namespace std;

int main(int argc, char *argv[])
{
  string mission_file;
  string run_command = argv[0];

  for(int i=1; i<argc; ++i) {
    string argi = argv[i];
    if((argi == "-v") || (argi == "--version") || (argi == "-version"))
      showReleaseInfoAndExit();
    else if((argi == "-e") || (argi == "--example") || (argi == "-example"))
      showExampleConfigAndExit();
    else if((argi == "-h") || (argi == "--help") || (argi == "-help"))
      showHelpAndExit();
    else if((argi == "-i") || (argi == "--interface"))
      showInterfaceAndExit();
    else if(strEnds(argi, ".moos") || strEnds(argi, ".moos++"))
      mission_file = argi;
    else if(strBegins(argi, "--alias="))
      run_command = argi.substr(8);
    else if(i == 2)
      run_command = argi;
  }

  if(mission_file == "")
    showHelpAndExit();

  cout << termColor("green")
       << "pThrustMixer launching as " << run_command << endl
       << termColor() << endl;

  ThrustMixer app;
  app.Run(run_command.c_str(), mission_file.c_str());
  return(0);
}
