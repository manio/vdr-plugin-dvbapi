/*
 *  vdr-plugin-dvbapi - softcam dvbapi plugin for VDR
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <getopt.h>
#include <vdr/plugin.h>
#include <linux/dvb/ca.h>
#include "DVBAPI.h"
#include "DVBAPISetup.h"
#include "Log.h"

DVBAPI::DVBAPI(void)
{
  dlls.Load();
  cScDevices::OnPluginLoad();
}

DVBAPI::~DVBAPI()
{
  cScDevices::OnPluginUnload();
}

const char *DVBAPI::CommandLineHelp(void)
{
  return ("  -B N,     --budget=N     forces DVB device N to budget mode (using FFdecsa)\n");
}

bool DVBAPI::ProcessArgs(int argc, char *argv[])
{
  static struct option long_options[] = {
      { "budget",      required_argument, NULL, 'B' },
      { NULL }
    };

  int c, option_index = 0;
  while ((c = getopt_long(argc, argv, "B:", long_options, &option_index)) != -1)
  {
    switch (c)
    {
      case 'B':
        cScDevices::SetForceBudget(atoi(optarg));
        break;
      default:
        return false;
    }
  }
  return true;
}

bool DVBAPI::Initialize(void)
{
  // Initialize any background activities the plugin shall perform.
  INFOLOG("plugin version %s initializing (VDR %s)", VERSION, VDRVERSION);
  decsa = new DeCSA(0);
  capmt = new CAPMT;
  return cScDevices::Initialize();
}

bool DVBAPI::Start(void)
{
  cScDevices::Startup();
  INFOLOG("plugin started");
  return true;
}

void DVBAPI::Stop(void)
{
  cScDevices::Shutdown();
  INFOLOG("plugin stopped");
}

void DVBAPI::Housekeeping(void)
{
  // Perform any cleanup or other regular tasks.
}

void DVBAPI::MainThreadHook(void)
{
  // Perform actions in the context of the main program thread.
  // WARNING: Use with great care - see PLUGINS.html!
}

cString DVBAPI::Active(void)
{
  // Return a message string if shutdown should be postponed
  return NULL;
}

time_t DVBAPI::WakeupTime(void)
{
  // Return custom wakeup time for shutdown script
  return 0;
}

cMenuSetupPage *DVBAPI::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return new cMenuSetupDVBAPI;
}

bool DVBAPI::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.
  if (!strcasecmp(Name, CONFNAME_LOGLEVEL))
    LogLevel = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_DECSABUFSIZE))
    DeCsaTsBuffSize = atoi(Value);
  else
    return false;
  return true;
}

bool DVBAPI::Service(const char *Id, void *Data)
{
  // Handle custom service requests from other plugins
  return false;
}

const char **DVBAPI::SVDRPHelpPages(void)
{
  // Return help text for SVDRP commands this plugin implements
  return NULL;
}

cString DVBAPI::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  // Process SVDRP commands this plugin implements
  return NULL;
}

VDRPLUGINCREATOR(DVBAPI);       // Don't touch this!
