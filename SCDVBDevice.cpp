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
#include "SCDVBDevice.h"
#include "SCDeviceProbe.h"




SCDVBDevice::SCDVBDevice(int adapter, int frontend, int cafd) :cDvbDevice(adapter,frontend)
{
  isReady=true;
  isyslog("DVBAPI: SCDVBDevice::SCDVBDevice adapter=%d frontend=%d", adapter,frontend );
  this->adapter = adapter; 
   tsBuffer=0; fullts=false;
  fd_ca=cafd; fd_ca2=dup(fd_ca); fd_dvr=-1;
  softcsa=(fd_ca<0);
  UDPSocket::bindx(this);
  decsa=new DeCSA(adapter);
  cAPMT=new CAPMT(adapter,frontend);
  sCCIAdapter=new SCCIAdapter(this,adapter);
  isyslog(" SCDVBDevice::SCDVBDevice Done.");
}

CAPMT *SCDVBDevice::GetCAPMT()
{
  return cAPMT;
}

void SCDVBDevice::SetReady(bool Ready)
{
  isReady = Ready;
}

void SCDVBDevice::CiStartDecrypting(void)
 {
  isyslog("DVBAPI: SCDVBDevice::CiStartDecrypting");
 }

 bool SCDVBDevice::CiAllowConcurrent(void)  const
 {
  isyslog("DVBAPI: SCDVBDevice::CiAllowConcurrent");
   return true;
 }


 bool SCDVBDevice::HasCi()
 {
  isyslog("DVBAPI: SCDVBDevice::HasCi");
  return true;
//  return sCCIAdapter || hWCIAdapter;
 }

  bool  SCDVBDevice::SetChannelDevice(const cChannel *Channel, bool LiveView)
 {
//  initialCaDscr=true;
//  isReady=false;
//  return cDvbDevice::SetChannelDevice(Channel,LiveView);

  // doing it the old way - need to FIX it later - maybe some of following code is not needed
  isReady=false;
  isyslog("DVBAPI: SCDVBDevice::SetChannelDevice");
  switchMutex.Lock();
  isyslog("DVBAPI: SCDVBDevice::SetChannelDevice  SOFTCAM_SWITCH....");
  bool ret=cDvbDevice::SetChannelDevice(Channel,LiveView);
  // HERE
  isyslog("SOFTCAM_SWITCH Device: '%d' Channel: '%d' SID '%d' ", this->DeviceNumber(), Channel->Number(), Channel->Sid());
  if(HasLock(5000))
  {
    isyslog("DVBAPI: SCDVBDevice::SetChannelDevice SOFTCAM_SWITCH HasLock");
    initialCaDscr=true;
    //cAPMT->send(Channel->Sid());
  }
  isyslog("DVBAPI: SCDVBDevice::SetChannelDevice SOFTCAM_SWITCH Finished ret=%d",ret);
  //isReady=true;
  switchMutex.Unlock();
  return ret;
 }



 bool SCDVBDevice::OpenDvr(void)
 {
  isyslog("DVBAPI: SCDVBDevice::OpenDvr");
  CloseDvr();
  fd_dvr=DvbOpen(DEV_DVB_DVR,DVB_DEV_SPEC,O_RDONLY|O_NONBLOCK,true);
  if(fd_dvr>=0)
  {
    tsMutex.Lock();
    tsBuffer=new DeCsaTsBuffer(fd_dvr,MEGABYTE(4),CardIndex()+1,decsa,ScActive());
    tsMutex.Unlock();
  }
  return fd_dvr>=0;
 }


 void SCDVBDevice::CloseDvr(void)
 {
  isyslog("DVBAPI: SCDVBDevice::CloseDvr");
  tsMutex.Lock();
  delete tsBuffer;
   tsBuffer=0;
  tsMutex.Unlock();
  if(fd_dvr>=0)
   {
	 close(fd_dvr);
	   fd_dvr=-1;
  }
 }

 bool SCDVBDevice::GetTSPacket(uchar *&Data)
 {
  if(tsBuffer)
  {

	  Data=tsBuffer->Get();
	  return true;
  }
  return false;
 }

SCDVBDevice::~SCDVBDevice()
{
  isyslog("DVBAPI: SCDVBDevice::~SCDVBDevice");
  DetachAllReceivers();
  Cancel(3);
  EarlyShutdown();
  if(decsa)
   delete decsa;
  decsa=0;
  if(fd_ca>=0) close(fd_ca);
  if(fd_ca2>=0) close(fd_ca2);
//  if(cADevice!=0)
//   delete cADevice;
//  cADevice=0;
  if(cAPMT!=0)
    delete cAPMT;
  cAPMT=0;
}

void SCDVBDevice::EarlyShutdown()
{
  isyslog("DVBAPI: SCDVBDevice::EarlyShutdown");
//  if(cADevice!=0)
//   delete cADevice;
//  cADevice=0;
  if(cAPMT!=0)
    delete cAPMT;
  cAPMT=0;

}

void SCDVBDevice::Shutdown(void)
{
 isyslog("DVBAPI: SCDVBDevice::Shutdown");
for(int n=cDevice::NumDevices(); --n>=0;)
 {
  SCDVBDevice *dev=dynamic_cast<SCDVBDevice *>(cDevice::GetDevice(n));
  if(dev)
	dev->EarlyShutdown();
 }
}

void SCDVBDevice::Startup(void)
{
//	  if(ScSetup.ForceTransfer)
//	    SetTransferModeForDolbyDigital(2);
 for(int n=cDevice::NumDevices(); --n>=0;)
 {
  SCDVBDevice *dev=dynamic_cast<SCDVBDevice *>(cDevice::GetDevice(n));
  if(dev)
   dev->LateInit();
 }
 isyslog("DVBAPI: SCDVBDevice::Startup");
}

void SCDVBDevice::SetForceBudget(int n)
{
 isyslog("DVBAPI: SCDVBDevice::SetForceBudget");
}

void SCDVBDevice::LateInit()
{
  isyslog("DVBAPI: SCDVBDevice::LateInit");
  int n=CardIndex();
  if(DeviceNumber()!=n)
   isyslog("CardIndex - DeviceNumber mismatch! Put SC plugin first on VDR commandline!");
   if(softcsa)
   {
	if(HasDecoder())
		isyslog("Card %d is a full-featured card but no ca device found!",n);
    }
	else //if(cScDevices::ForceBudget(n))
	{
	  isyslog("Budget mode forced on card %d",n);
 	  softcsa=true;
	}
//	if(fd_ca2>=0)
 //	 hWCIAdapter=cDvbCiAdapter::CreateCiAdapter(this,fd_ca2);
//	  cam=new Cam(this,n);
 //   sCCIAdapter=new SCCIAdapter(this,n,cam);
//    SetCamSlot(sCCIAdapter->getCamSlot());
	if(softcsa)
	{
	  if(IsPrimaryDevice() && HasDecoder())
	  {
		isyslog("Enabling hybrid full-ts mode on card %d",n);
	    fullts=true;
	   }
	   else isyslog("Using software decryption on card %d",n);
	  }
}

bool SCDVBDevice::ForceBudget(int n)
{
  isyslog("DVBAPI: CDVBDevice::ForceBudget");
   return true;
}

void SCDVBDevice::Capture(void)
{
	  isyslog("DVBAPI: SCDVBDevice::Capture");
}

void SCDVBDevice::DvbName(const char *Name, int a, int f, char *buffer, int len)
{
  snprintf(buffer,len,"%s%d/%s%d",DEV_DVB_ADAPTER,a,Name,f);
}

int SCDVBDevice::DvbOpen(const char *Name, int a, int f, int Mode, bool ReportError)
{
  char FileName[128];
  DvbName(Name,a,f,FileName,sizeof(FileName));
  int fd=open(FileName,Mode);
  if(fd<0 && ReportError) LOG_ERROR_STR(FileName);
  return fd;
}

void SCDVBDevice::OnPluginLoad(void)
{
  isyslog("SCDVBDevice::OnPluginLoad");
  SCDeviceProbe::Install();
}

void SCDVBDevice::OnPluginUnload(void)
{
    isyslog("SCDVBDevice::OnPluginUnload");
	SCDeviceProbe::Remove();
}

bool SCDVBDevice::Ready(void)
{
//  isyslog("SCDVBDevice::Ready");
  return isReady;
//  return (sCCIAdapter   ? sCCIAdapter->Ready():true) &&
//         (hWCIAdapter ? hWCIAdapter->Ready():true);
}


bool SCDVBDevice::Initialize(void)
{
  isyslog("SCDVBDevice::Initialize");
  return true;
}

void SCDVBDevice::CaidsChanged(void)
{
    isyslog("SCDVBDevice::CaidsChanged");
  //  if(sCCIAdapter) sCCIAdapter->CaidsChanged();
}

bool SCDVBDevice::SoftCSA(bool live)
{
  isyslog("SCDVBDevice::SoftCSiA");
  return softcsa && (!fullts || !live);
}

bool SCDVBDevice::SetCaDescr(ca_descr_t *ca_descr)
{
 isyslog("SCDVBDevice::SetCaDescr index=%d, Ready()=%d", ca_descr->index, Ready());
// if(!softcsa || (fullts && ca_descr->index==0))
// {
//   cMutexLock lock(&cafdMutex);
//   return ioctl(fd_ca,CA_SET_DESCR,ca_descr)>=0;
// }
    if (ca_descr->index == -1)
    {
	isyslog("SCDVBDevice::SetCaDescr removal request - ignoring");
	return true;
    }
 if(!Ready())
   return Ready();
  bool ret=decsa->SetDescr(ca_descr,initialCaDscr);
  initialCaDscr=false;
  return ret;
}

bool SCDVBDevice::SetCaPid(ca_pid_t *ca_pid)
{
  isyslog("SCDVBDevice::SetCaPid PID=%d, index=%d, Ready()=%d", ca_pid->pid, ca_pid->index, Ready());
 // if(!softcsa || (fullts && ca_pid->index==0))
 // {
  //  cMutexLock lock(&cafdMutex);
 //   return ioctl(fd_ca,CA_SET_PID,ca_pid)>=0;
 // }
    if (ca_pid->index == -1)
    {
	isyslog("SCDVBDevice::SetCaPid removal request - ignoring");
	return true;
    }
  if(!Ready())
    return Ready();
   return decsa->SetCaPid(ca_pid);
}
 bool SCDVBDevice::SetPid(cPidHandle *Handle, int Type, bool On)
 {

  isyslog("SCDVBDevice::SetPid on=%d",On);
//  if(!On)
//	 unlink(fnam);
  //if(cam) cam->SetPid(Type,Handle->pid,On);
  tsMutex.Lock();
  if(tsBuffer) tsBuffer->SetActive(ScActive());
  tsMutex.Unlock();
  return cDvbDevice::SetPid(Handle,Type,On);
}

bool SCDVBDevice::ScActive(void)
{
  isyslog("SCDVBDevice::ScActive");
  return true;//dynamic_cast<SCCAMSlot *>(CamSlot())!=0;
}


