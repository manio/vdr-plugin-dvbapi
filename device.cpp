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

// -- cScDvbDevice -------------------------------------------------------------

#define SCDEVICE cScDvbDevice
#define DVBDEVICE cDvbDevice
#include "device-tmpl.cpp"

// -- cScDvbDevicePlugin -------------------------------------------------------

class cScDvbDevicePlugin : public cScDevicePlugin
{
public:
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId);
  virtual bool LateInit(cDevice *dev);
  virtual bool EarlyShutdown(cDevice *dev);
};

cDevice *cScDvbDevicePlugin::Probe(int Adapter, int Frontend, uint32_t SubSystemId)
{
  INFOLOG("creating standard device %d/%d", Adapter, Frontend);
  return new cScDvbDevice(this, Adapter, Frontend, cScDevices::DvbOpen(DEV_DVB_CA, Adapter, Frontend, O_RDWR));
}

bool cScDvbDevicePlugin::LateInit(cDevice *dev)
{
  cScDvbDevice *d = dynamic_cast<cScDvbDevice *>(dev);
  if (d)
    d->LateInit();
  return d != 0;
}

bool cScDvbDevicePlugin::EarlyShutdown(cDevice *dev)
{
  cScDvbDevice *d = dynamic_cast<cScDvbDevice *>(dev);
  if (d)
    d->EarlyShutdown();
  return d != 0;
}

// -- cScDevices ---------------------------------------------------------------

int cScDevices::budget = 0;

void cScDevices::DvbName(const char *Name, int a, int f, char *buffer, int len)
{
  snprintf(buffer, len, "%s/%s%d/%s%d", DEV_DVB_BASE, DEV_DVB_ADAPTER, a, Name, f);
}

int cScDevices::DvbOpen(const char *Name, int a, int f, int Mode, bool ReportError)
{
  char FileName[128];
  DvbName(Name, a, f, FileName, sizeof(FileName));
  int fd = open(FileName, Mode);
  if (fd < 0 && ReportError)
    LOG_ERROR_STR(FileName);
  return fd;
}

void cScDevices::OnPluginLoad(void)
{
  cScDeviceProbe::Install();
  // default device plugin must be last in the list
  new cScDvbDevicePlugin;
}

void cScDevices::OnPluginUnload(void)
{
  cScDeviceProbe::Remove();
}

bool cScDevices::Initialize(void)
{
  return true;
}

void cScDevices::Startup(void)
{
  for (int n = cDevice::NumDevices(); --n >= 0;)
  {
    cDevice *dev = cDevice::GetDevice(n);
    for (cScDevicePlugin *dp = devplugins.First(); dp; dp = devplugins.Next(dp))
      if (dp->LateInit(dev))
        break;
  }
}

void cScDevices::Shutdown(void)
{
  for (int n = cDevice::NumDevices(); --n >= 0;)
  {
    cDevice *dev = cDevice::GetDevice(n);
    for (cScDevicePlugin *dp = devplugins.First(); dp; dp = devplugins.Next(dp))
      if (dp->EarlyShutdown(dev))
        break;
  }
}

void cScDevices::SetForceBudget(int n)
{
  if (n >= 0 && n < MAXDVBDEVICES)
    budget |= (1 << n);
}

bool cScDevices::ForceBudget(int n)
{
  return budget && (budget & (1 << n));
}
