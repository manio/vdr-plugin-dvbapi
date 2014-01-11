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
#include "SocketHandler.h"
#include "SCCIAdapter.h"

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
  return "";
}

bool DVBAPI::ProcessArgs(int argc, char *argv[])
{
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
  decsa = new DeCSA(0);
  capmt = new CAPMT;
  SocketHandler::bindx();

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
  SocketHandler::unbind();
  delete capmt;
  capmt = NULL;
  delete decsa;
  decsa = NULL;
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
