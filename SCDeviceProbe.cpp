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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dlfcn.h>

#include <linux/dvb/ca.h>
#include <vdr/channels.h>
#include <vdr/ci.h>
#include <vdr/dvbdevice.h>
#include <vdr/dvbci.h>
#include <vdr/thread.h>
#include "SCDeviceProbe.h"
#include "Log.h"

cScDeviceProbe *cScDeviceProbe::probe = 0;

void cScDeviceProbe::Install(void)
{
  if (!probe)
    probe = new cScDeviceProbe;
}

void cScDeviceProbe::Remove(void)
{
  delete probe;
  probe = 0;
}

bool cScDeviceProbe::Probe(int Adapter, int Frontend)
{
  uint32_t subid = GetSubsystemId(Adapter, Frontend);
  INFOLOG("%s: capturing device %d/%d (subsystem ID %08x)", __FUNCTION__, Adapter, Frontend, subid);
  for (cScDevicePlugin *dp = devplugins.First(); dp; dp = devplugins.Next(dp))
    if (dp->Probe(Adapter, Frontend, subid))
      return true;
  return false;
}
