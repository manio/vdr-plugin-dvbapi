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

#ifdef WITH_HDDVB

// -- cScDvbHdFfDevice ---------------------------------------------------------

#include "../dvbhddevice/dvbhdffdevice.h"
#define SCDEVICE cScDvbHdFfDevice
#define DVBDEVICE cDvbHdFfDevice
#include "device-tmpl.cpp"

// -- cScHdDevicePlugin --------------------------------------------------------

class cScHdDevicePlugin : public cScDevicePlugin
{
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
};

static cScHdDevicePlugin _hddevplugin;

cDevice *cScHdDevicePlugin::Probe(int Adapter, int Frontend, uint32_t SubSystemId)
{
  static uint32_t SubsystemIds[] = {
    0x13C23009, // Technotrend S2-6400 HDFF development samples
    0x13C2300A, // Technotrend S2-6400 HDFF production version
    0x00000000
  };
  for (uint32_t *sid = SubsystemIds; *sid; sid++)
  {
    if (*sid == SubSystemId)
    {
      int fd = cScDevices::DvbOpen(DEV_DVB_OSD, Adapter, 0, O_RDWR);
      if (fd >= 0)
      {
        close(fd);
        INFOLOG("creating HD-FF device %d/%d", Adapter, Frontend);
        return new cScDvbHdFfDevice(this, Adapter, Frontend, cScDevices::DvbOpen(DEV_DVB_CA, Adapter, Frontend, O_RDWR));
      }
    }
  }
  return 0;
}

bool cScHdDevicePlugin::LateInit(cDevice *dev)
{
  cScDvbHdFfDevice *d = dynamic_cast<cScDvbHdFfDevice *>(dev);
  if (d)
    d->LateInit();
  return d != 0;
}

bool cScHdDevicePlugin::EarlyShutdown(cDevice *dev)
{
  cScDvbHdFfDevice *d = dynamic_cast<cScDvbHdFfDevice *>(dev);
  if (d)
    d->EarlyShutdown();
  return d != 0;
}

#endif //WITH_HDDVB
