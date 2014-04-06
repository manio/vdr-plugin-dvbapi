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

#ifndef ___DVBAPISETUP_H
#define ___DVBAPISETUP_H

#include <vdr/plugin.h>

#define CONFNAME_LOGLEVEL      "LogLevel"
#define CONFNAME_OSCAMNETWORK  "OSCamNetwork"
#define CONFNAME_OSCAMHOST     "OSCamHost"
#define CONFNAME_OSCAMPORT     "OSCamPort"

class cMenuSetupDVBAPI : public cMenuSetupPage
{
private:
  int newLogLevel;
  int newOSCamNetworkMode;
  char newOSCamHost[HOST_NAME_MAX];
  int newOSCamPort;
  void Setup(void);
protected:
  virtual eOSState ProcessKey(eKeys Key);
  virtual void Store(void);
public:
  cMenuSetupDVBAPI(void);
};

#endif // ___DVBAPISETUP_H
