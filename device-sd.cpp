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

#include "device.h"
#include "Log.h"

#ifdef WITH_SDDVB

// -- cScDvbSdFfDevice ---------------------------------------------------------

#include "../dvbsddevice/dvbsdffdevice.h"
#define SCDEVICE cScDvbSdFfDevice
#define DVBDEVICE cDvbSdFfDevice
#define OWN_FULLTS
#if APIVERSNUM >= 10721
#define OWN_DEVPARAMS false
#endif
#include "device-tmpl.cpp"

bool cScDvbSdFfDevice::CheckFullTs(void)
{
  return IsPrimaryDevice() && HasDecoder();
}

// -- cScSdDevicePlugin --------------------------------------------------------

class cScSdDevicePlugin : public cScDevicePlugin
{
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
};

static cScSdDevicePlugin _sddevplugin;

cDevice *cScSdDevicePlugin::Probe(int Adapter, int Frontend, uint32_t SubSystemId)
{
  static uint32_t SubsystemIds[] = {
    0x110A0000, // Fujitsu Siemens DVB-C
    0x13C20000, // Technotrend/Hauppauge WinTV DVB-S rev1.X or Fujitsu Siemens DVB-C
    0x13C20001, // Technotrend/Hauppauge WinTV DVB-T rev1.X
    0x13C20002, // Technotrend/Hauppauge WinTV DVB-C rev2.X
    0x13C20003, // Technotrend/Hauppauge WinTV Nexus-S rev2.X
    0x13C20004, // Galaxis DVB-S rev1.3
    0x13C20006, // Fujitsu Siemens DVB-S rev1.6
    0x13C20008, // Technotrend/Hauppauge DVB-T
    0x13C2000A, // Technotrend/Hauppauge WinTV Nexus-CA rev1.X
    0x13C2000E, // Technotrend/Hauppauge WinTV Nexus-S rev2.3
    0x13C21002, // Technotrend/Hauppauge WinTV DVB-S rev1.3 SE
    0x00000000
  };
  for (uint32_t *sid = SubsystemIds; *sid; sid++)
  {
    if (*sid == SubSystemId)
    {
      INFOLOG("creating SD-FF device %d/%d", Adapter, Frontend);
      return new cScDvbSdFfDevice(this, Adapter, Frontend, cScDevices::DvbOpen(DEV_DVB_CA, Adapter, Frontend, O_RDWR));
    }
  }
  return 0;
}

bool cScSdDevicePlugin::LateInit(cDevice *dev)
{
  cScDvbSdFfDevice *d = dynamic_cast<cScDvbSdFfDevice *>(dev);
  if (d)
    d->LateInit();
  return d != 0;
}

bool cScSdDevicePlugin::EarlyShutdown(cDevice *dev)
{
  cScDvbSdFfDevice *d = dynamic_cast<cScDvbSdFfDevice *>(dev);
  if (d)
    d->EarlyShutdown();
  return d != 0;
}

#endif //WITH_SDDVB
