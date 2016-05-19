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
char OSCamHost[HOST_NAME_MAX] = "localhost";
int OSCamPort = 2000;

cMenuSetupDVBAPI::cMenuSetupDVBAPI(void)
{
  newLogLevel = LogLevel;
  strn0cpy(newOSCamHost, OSCamHost, sizeof(newOSCamHost));
  newOSCamPort = OSCamPort;
  Add(new cMenuEditStrItem( *cString::sprintf("OSCam %s", tr("Host")), newOSCamHost, sizeof(newOSCamHost)));
  Add(new cMenuEditIntItem( *cString::sprintf("OSCam %s", tr("Port")), &newOSCamPort, 1, 0xffff));
  Add(new cMenuEditIntItem( tr("Log level (0-3)"), &newLogLevel, 0, 3));
}

void cMenuSetupDVBAPI::Store(void)
{
  if (newLogLevel > 3)
    newLogLevel = 3;
  SetupStore(CONFNAME_LOGLEVEL, LogLevel = newLogLevel);
  SetupStore(CONFNAME_OSCAMHOST, strn0cpy(OSCamHost, newOSCamHost, sizeof(OSCamHost)));
  SetupStore(CONFNAME_OSCAMPORT, OSCamPort = newOSCamPort);
}
