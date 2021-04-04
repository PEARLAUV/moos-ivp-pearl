/*
 * PEARLSimMain.cpp
 * 
 * Created on: 1/27/2021
 * Author: Ethan Rolland
 * Adapted from: 
 *
 */
 
#include <string>
#include "PEARLSim.h"
#include "PEARLSim_Info.h"
#include "MBUtils.h"

using namespace std;

int main(int argc, char *argv[])
{
  string mission_file;
  string run_command = argv[0];
  string incoming_var;
  string outgoing_var;

  for (int i = 1; i < argc; i++) {
    string argi = argv[i];
    if ((argi == "-e") || (argi == "--example") || (argi == "-example"))
      showExampleConfigAndExit();
    else if ((argi == "-h") || (argi == "--help") || (argi=="-help"))
      showHelpAndExit();
    else if ((argi == "-i") || (argi == "--interface"))
      showInterfaceAndExit();
    else if (strEnds(argi, ".moos") || strEnds(argi, ".moos++"))
      mission_file = argv[i];
    else if (strBegins(argi, "--alias="))
      run_command = argi.substr(8);
    else if (strBegins(argi, "--in="))
      incoming_var = argi.substr(5);
    else if (strBegins(argi, "--out="))
      outgoing_var = argi.substr(6);
    else if (i == 2)
      run_command = argi; }

  if (mission_file == "")
    showHelpAndExit();

  PEARLSim simpearl;
  simpearl.Run(run_command.c_str(), mission_file.c_str());

  return 0;
}

