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

#ifndef ___DEVICE_H
#define ___DEVICE_H

#include <vdr/dvbdevice.h>
#include "deviceplugin.h"

#if APIVERSNUM < 10723
#define DEV_DVB_BASE     "/dev/dvb"
#define DEV_DVB_ADAPTER  "adapter"
#define DEV_DVB_FRONTEND "frontend"
#define DEV_DVB_DVR      "dvr"
#define DEV_DVB_DEMUX    "demux"
#define DEV_DVB_CA       "ca"
#define DEV_DVB_OSD      "osd"
#endif

#define DVB_DEV_SPEC adapter,frontend

class cScDevices : public cDvbDevice
{
private:
  static int budget;

public:
  static void OnPluginLoad(void);
  static void OnPluginUnload(void);
  static bool Initialize(void);
  static void Startup(void);
  static void Shutdown(void);
  static void SetForceBudget(int n);
  static bool ForceBudget(int n);
  static void DvbName(const char *Name, int a, int f, char *buffer, int len);
  static int DvbOpen(const char *Name, int a, int f, int Mode, bool ReportError = false);
};

#endif // ___DEVICE_H
