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
  isyslog("DVBAPI: SCDeviceProbe::Probe capturing device %d/%d", Adapter, Frontend);
  new SCDVBDevice(Adapter, Frontend, SCDVBDevice::DvbOpen(DEV_DVB_CA, Adapter, Frontend, O_RDWR));
  return true;
}
