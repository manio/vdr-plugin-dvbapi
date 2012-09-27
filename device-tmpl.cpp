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

// -- cScDevice ----------------------------------------------------------------

#include "SCCIAdapter.h"
#include "DeCsaTSBuffer.h"

class SCDEVICE : public DVBDEVICE
{
private:
  DeCsaTsBuffer *tsBuffer;
  cMutex tsMutex;
  cScDevicePlugin *devplugin;
  cCiAdapter *hwciadapter;
  cTimeMs lastDump;
  SCCIAdapter *sCCIAdapter;
  int fd_dvr, fd_ca, fd_ca2;
  bool softcsa, fullts;
  char devId[8];
  bool ScActive(void);

protected:
  virtual bool Ready(void);
  virtual bool SetPid(cPidHandle *Handle, int Type, bool On);
  virtual bool SetChannelDevice(const cChannel *Channel, bool LiveView);
  virtual bool OpenDvr(void);
  virtual void CloseDvr(void);
  virtual bool GetTSPacket(uchar *&Data);

public:
  SCDEVICE(cScDevicePlugin *DevPlugin, int Adapter, int Frontend, int cafd);
  ~SCDEVICE();
  virtual bool HasCi(void);
  void LateInit(void);
  void EarlyShutdown(void);
  bool CheckFullTs(void);
};

SCDEVICE::SCDEVICE(cScDevicePlugin *DevPlugin, int Adapter, int Frontend, int cafd)
#ifdef OWN_DEVPARAMS
 : DVBDEVICE(Adapter, Frontend, OWN_DEVPARAMS)
#else
 : DVBDEVICE(Adapter, Frontend)
#endif //OWN_DEVPARAMS
{
  DEBUGLOG("%s: adapter=%d frontend=%d", __FUNCTION__, Adapter, Frontend);
  tsBuffer = 0;
  hwciadapter = 0;
  devplugin = DevPlugin;
  softcsa = fullts = false;
  fd_ca = cafd;
  fd_ca2 = dup(fd_ca);
  fd_dvr = -1;
  snprintf(devId, sizeof(devId), "%d/%d", Adapter, Frontend);
  DEBUGLOG("%s: done", __FUNCTION__);
}

SCDEVICE::~SCDEVICE()
{
  DEBUGLOG("%s", __FUNCTION__);
  DetachAllReceivers();
  Cancel(3);
  EarlyShutdown();
  if (fd_ca >= 0)
    close(fd_ca);
  if (fd_ca2 >= 0)
    close(fd_ca2);
}

void SCDEVICE::EarlyShutdown(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  SetCamSlot(0);
  if (sCCIAdapter)
    delete sCCIAdapter;
  sCCIAdapter = 0;
  delete hwciadapter;
  hwciadapter = 0;
}

#ifndef OWN_FULLTS
bool SCDEVICE::CheckFullTs(void)
{
  return false;
}
#endif //!OWN_FULLTS

void SCDEVICE::LateInit(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  int n = CardIndex();
  if (DeviceNumber() != n)
    ERRORLOG("CardIndex - DeviceNumber mismatch! Put DVBAPI plugin first on VDR commandline!");
  softcsa = (fd_ca < 0);
  if (softcsa)
  {
    if (HasDecoder())
      INFOLOG("Card %s is a full-featured card but no ca device found!", devId);
  }
  else if (cScDevices::ForceBudget(n))
  {
    INFOLOG("Budget mode forced on card %s", devId);
    softcsa = true;
  }
  if (softcsa)
  {
    fullts = CheckFullTs();
    if (fullts)
      INFOLOG("Enabling hybrid full-ts mode on card %s", devId);
    else
      INFOLOG("Using software decryption on card %s", devId);
  }
  if (fd_ca2 >= 0)
    hwciadapter = cDvbCiAdapter::CreateCiAdapter(this, fd_ca2);
  sCCIAdapter = new SCCIAdapter(this, n, fd_ca, softcsa, fullts);
}

bool SCDEVICE::HasCi(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  return sCCIAdapter || hwciadapter;
}

bool SCDEVICE::Ready(void)
{
  return (sCCIAdapter ? sCCIAdapter->Ready() : true) &&
         (hwciadapter ? hwciadapter->Ready() : true);
}

bool SCDEVICE::SetPid(cPidHandle *Handle, int Type, bool On)
{
  DEBUGLOG("%s: on=%d", __FUNCTION__, On);
  tsMutex.Lock();
  if (tsBuffer)
    tsBuffer->SetActive(ScActive());
  tsMutex.Unlock();
  return DVBDEVICE::SetPid(Handle, Type, On);
}

bool SCDEVICE::SetChannelDevice(const cChannel *Channel, bool LiveView)
{
  DEBUGLOG("%s", __FUNCTION__);
  bool ret = DVBDEVICE::SetChannelDevice(Channel, LiveView);
  if (LiveView && IsPrimaryDevice() && Channel->Ca() >= CA_ENCRYPTED_MIN && !Transferring() && softcsa && !fullts)
  {
    INFOLOG("Forcing transfermode on card %s", devId);
    DVBDEVICE::SetChannelDevice(Channel, false); // force transfermode
  }
  return ret;
}

bool SCDEVICE::ScActive(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  return true;
}

bool SCDEVICE::OpenDvr(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  CloseDvr();
  fd_dvr = cScDevices::DvbOpen(DEV_DVB_DVR, DVB_DEV_SPEC, O_RDONLY | O_NONBLOCK, true);
  if (fd_dvr >= 0)
  {
    tsMutex.Lock();
    tsBuffer = new DeCsaTsBuffer(fd_dvr, MEGABYTE(DeCsaTsBuffSize), CardIndex() + 1, sCCIAdapter->GetDeCSA(), ScActive());
    tsMutex.Unlock();
  }
  return fd_dvr >= 0;
}

void SCDEVICE::CloseDvr(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  tsMutex.Lock();
  delete tsBuffer;
  tsBuffer = 0;
  tsMutex.Unlock();
  if (fd_dvr >= 0)
  {
    close(fd_dvr);
    fd_dvr = -1;
  }
}

bool SCDEVICE::GetTSPacket(uchar *&Data)
{
  if (tsBuffer)
  {
    Data = tsBuffer->Get();
    return true;
  }
  return false;
}

#undef SCDEVICE
#undef DVBDEVICE
#undef OWN_FULLTS
#undef OWN_DEVPARAMS
