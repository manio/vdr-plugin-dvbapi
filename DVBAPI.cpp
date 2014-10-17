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
#include "DVBAPI.h"
#include "DVBAPISetup.h"
#include "Log.h"

#ifndef LIBDVBCSA
static const char *DECSALIB       = "FFdecsa";
#else
static const char *DECSALIB       = "libdvbcsa";
#endif

bool CheckExpiredCW = true;
unsigned int AdapterIndexOffset = 0;

DVBAPI::DVBAPI(void)
{
  for (int i = 0; i < MAXDEVICES; i++)
    sCCIAdapter[i] = NULL;
}

DVBAPI::~DVBAPI()
{
}

const char *DVBAPI::CommandLineHelp(void)
{
  return "  -o N,     --offset=N     add constant value to all adapter indexes when\n"
         "                           communicating with OSCam (default: 0)\n"
         "  -d,       --disable-exp  disable CW expiration check\n";
}

bool DVBAPI::ProcessArgs(int argc, char *argv[])
{
  static struct option long_options[] = {
      { "offset",      required_argument, NULL, 'o' },
      { "disable-exp", no_argument,       NULL, 'd' },
      { NULL }
    };

  int c, option_index = 0;
  while ((c = getopt_long(argc, argv, "o:d", long_options, &option_index)) != -1)
  {
    switch (c)
    {
      case 'o':
        AdapterIndexOffset = atoi(optarg);
        INFOLOG("Using value %d as the adapter index offset", AdapterIndexOffset);
        break;
      case 'd':
        CheckExpiredCW = false;
        INFOLOG("CW expiration check is disabled");
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
  return true;
}

bool DVBAPI::Start(void)
{
  INFOLOG("plugin version %s initializing (VDR %s)", VERSION, VDRVERSION);
  INFOLOG("decryption library: %s", DECSALIB);
  capmt = new CAPMT;
  decsa = new DeCSA(0);
  filter = new cDvbapiFilter;
  SockHandler = new SocketHandler;

  for (int i = 0; i < cDevice::NumDevices(); i++)
  {
    if (const cDevice *Device = cDevice::GetDevice(i))
    {
      INFOLOG("Creating sCCIAdapter for device %d", Device->CardIndex());
      sCCIAdapter[i] = new SCCIAdapter(NULL, Device->CardIndex(), 0, true, true);
    }
  }
  INFOLOG("plugin started");
  return true;
}

void DVBAPI::Stop(void)
{
  for (int i = 0; i < MAXDEVICES; i++)
  {
    delete sCCIAdapter[i];
    sCCIAdapter[i] = NULL;
  }
  delete SockHandler;
  SockHandler = NULL;
  delete filter;
  filter = NULL;
  delete decsa;
  decsa = NULL;
  delete capmt;
  capmt = NULL;
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
  else if (!strcasecmp(Name, CONFNAME_OSCAMNETWORK))
    OSCamNetworkMode = atoi(Value);
  else if (!strcasecmp(Name, CONFNAME_OSCAMHOST))
    strn0cpy(OSCamHost, Value, sizeof(OSCamHost));
  else if (!strcasecmp(Name, CONFNAME_OSCAMPORT))
    OSCamPort = atoi(Value);
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
