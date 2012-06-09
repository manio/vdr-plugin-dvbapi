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

#ifdef WITH_UFS9XX

// -- cScDvbHdDevice ---------------------------------------------------------

#include "../dvbufs9xx/dvbhddevice.h"
#define SCDEVICE cScDvbHdDevice
#define DVBDEVICE cDvbHdDevice
#include "device-tmpl.cpp"

// -- cScUfsDevicePlugin --------------------------------------------------------

class cScUfsDevicePlugin : public cScDevicePlugin
{
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
};

static cScUfsDevicePlugin _ufsdevplugin;

cDevice *cScUfsDevicePlugin::Probe(int Adapter, int Frontend, uint32_t SubSystemId)
{
  // unfortunately we can't rely on subsystem ID, because it was set to 00000000 on Kathrein device
  INFOLOG("creating UFS 9xx device %d/%d", Adapter, Frontend);
  return new cScDvbHdDevice(this, Adapter, Frontend, cScDevices::DvbOpen(DEV_DVB_CA, Adapter, Frontend, O_RDWR));
}

bool cScUfsDevicePlugin::LateInit(cDevice *dev)
{
  cScDvbHdDevice *d = dynamic_cast<cScDvbHdDevice *>(dev);
  if (d)
    d->LateInit();
  return d != 0;
}

bool cScUfsDevicePlugin::EarlyShutdown(cDevice *dev)
{
  cScDvbHdDevice *d = dynamic_cast<cScDvbHdDevice *>(dev);
  if (d)
    d->EarlyShutdown();
  return d != 0;
}

#endif //WITH_UFS9XX
