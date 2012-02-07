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
#include "SCDVBDevice.h"
#include "Log.h"

SCDeviceProbe *SCDeviceProbe::probe = 0;

void SCDeviceProbe::Install(void)
{
  if (!probe)
    probe = new SCDeviceProbe;
}

void SCDeviceProbe::Remove(void)
{
  if (probe != 0)
    delete probe;
  probe = 0;
}

bool SCDeviceProbe::Probe(int Adapter, int Frontend)
{
  INFOLOG("%s: capturing device %d/%d", __FUNCTION__, Adapter, Frontend);
  new SCDVBDevice(Adapter, Frontend, SCDVBDevice::DvbOpen(DEV_DVB_CA, Adapter, Frontend, O_RDWR));
  return true;
}
