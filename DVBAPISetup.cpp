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

#include "DVBAPISetup.h"

int LogLevel = 2;
int OSCamNetworkMode = 0;
char OSCamHost[HOST_NAME_MAX] = "localhost";
char OSCamPort[HOST_NAME_MAX] = "2000";

cMenuSetupDVBAPI::cMenuSetupDVBAPI(void)
{
  newLogLevel = LogLevel;
  newOSCamNetworkMode = OSCamNetworkMode;
  strn0cpy(newOSCamHost, OSCamHost, sizeof(newOSCamHost));
  strn0cpy(newOSCamPort, OSCamPort, sizeof(newOSCamPort));
  Setup();
}

void cMenuSetupDVBAPI::Setup(void)
{
  int current = Current();

  Clear();

  Add(new cMenuEditIntItem( tr("Log level (0-3)"), &newLogLevel, 0, 3));
  Add(new cMenuEditBoolItem( tr("Enable OSCam network mode"), &newOSCamNetworkMode));
  if (newOSCamNetworkMode)
  {
    Add(new cMenuEditStrItem( *cString::sprintf(" %s", tr("Host")), newOSCamHost, sizeof(newOSCamHost)));
    Add(new cMenuEditStrItem( *cString::sprintf(" %s", tr("Port")), newOSCamPort, sizeof(newOSCamPort)));
  }

  SetCurrent(Get(current));
  Display();
}

eOSState cMenuSetupDVBAPI::ProcessKey(eKeys Key)
{
  int oldOSCamNetworkMode = newOSCamNetworkMode;
  eOSState state = cMenuSetupPage::ProcessKey(Key);

  if (Key != kNone && oldOSCamNetworkMode != newOSCamNetworkMode)
    Setup();

  return state;
}

void cMenuSetupDVBAPI::Store(void)
{
  if (newLogLevel > 3)
    newLogLevel = 3;
  SetupStore(CONFNAME_LOGLEVEL, LogLevel = newLogLevel);
  SetupStore(CONFNAME_OSCAMNETWORK, OSCamNetworkMode = newOSCamNetworkMode);
  SetupStore(CONFNAME_OSCAMHOST, strn0cpy(OSCamHost, newOSCamHost, sizeof(OSCamHost)));
  SetupStore(CONFNAME_OSCAMPORT, strn0cpy(OSCamPort, newOSCamPort, sizeof(OSCamPort)));
}
