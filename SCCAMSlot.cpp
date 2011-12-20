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
#include "SCCIAdapter.h"
#include "SCCAMSlot.h"

// from vdr's ci.c
#define AOT_CA_INFO_ENQ             0x9F8030
#define AOT_CA_INFO                 0x9F8031
#define AOT_CA_PMT                  0x9f8032


SCCAMSlot::SCCAMSlot(SCCIAdapter *sCCIAdapter, int cardIndex, int slot) :cCamSlot(sCCIAdapter),checkTimer(-SLOT_CAID_CHECK-1000),rb(KILOBYTE(4),5+LEN_OFF,false,"SC-CI slot answer")
{
  this->sCCIAdapter=sCCIAdapter;
  this->cardIndex=cardIndex;
  this->slot=slot;
  version=0; caids[0]=0; doReply=false; lastStatus=msReset;
  frame.SetRb(&rb);
  Reset(false);
}

eModuleStatus SCCAMSlot::Status(void)
{
  eModuleStatus status;
  if(reset)
  {
    status=msReset;
    if(resetTimer.TimedOut())
    	reset=false;
  }
  else	if(caids[0])
	  status=msReady;
  else
  {
    status=msPresent; //msNone;
    Check();
   }
  if(status!=lastStatus)
  {
    static const char *stext[] = { "none","reset","present","ready" };
    isyslog("DVBAPI: SCCAMSlot::Status %d.%d: status '%s'",cardIndex,slot,stext[status]);
    lastStatus=status;
  }
  return status;
}


bool SCCAMSlot::Reset(bool log)
{
  isyslog("DVBAPI: SCCAMSlot::Reset log=%i",log);
  reset=true;
  resetTimer.Set(SLOT_RESET_TIME);
  rb.Clear();
//  if(log) PRINTF(L_CORE_CI,"%d.%d: reset",cardIndex,slot);
  isyslog("DVBAPI: SCCAMSlot::Reset Done");
  return reset;
}

bool SCCAMSlot::Check(void)
{
//  isyslog("DVBAPI: SCCAMSlot::Check");
  bool res=false;
  bool dr=true;//ciadapter->CamSoftCSA() || ScSetup.ConcurrentFF>0;
  if(dr!=doReply && !IsDecrypting())
  {
    isyslog("DVBAPI: SCCAMSlot::Check  %d.%d: doReply changed, reset triggered",cardIndex,slot);
    Reset(false);
    doReply=dr;
    }
  if(checkTimer.TimedOut())
  {
    if(version!=sCCIAdapter->GetCaids(slot,0,0))
    {
     version=sCCIAdapter->GetCaids(slot,caids,MAX_CI_SLOT_CAIDS);
//   isyslog("DVBAPI: SCCAMSlot::Check %d.%d: now using CAIDs version %d",cardIndex,slot,version);
     res=true;
    }
    checkTimer.Set(SLOT_CAID_CHECK);
   }
  return res;
}

int SCCAMSlot::GetLength(const unsigned char * &data)
{
//   isyslog("DVBAPI: SCCAMSlot::GetLength");
  int len=*data++;
  if(len&TDPU_SIZE_INDICATOR) {
    int i;
    for(i=len&~TDPU_SIZE_INDICATOR, len=0; i>0; i--) len=(len<<8) + *data++;
    }
  return len;
}

int SCCAMSlot::LengthSize(int n)
{
//  isyslog("DVBAPI: SCCAMSlot::LengthSize");
  return n<TDPU_SIZE_INDICATOR?1:3;
}

void SCCAMSlot::SetSize(int n, unsigned char * &p)
{
//  isyslog("DVBAPI: SCCAMSlot::SetSize");
  if(n<TDPU_SIZE_INDICATOR) *p++=n;
  else { *p++=2|TDPU_SIZE_INDICATOR; *p++=n>>8; *p++=n&0xFF; }
}

void SCCAMSlot::CaInfo(int tcid, int cid)
{
  int cn=0;
  for(int i=0; caids[i]; i++)
	  cn+=2;
   int n=cn+8+LengthSize(cn);
  unsigned char *p;
  if(!(p=frame.GetBuff(n+1+LengthSize(n))))
	 return;
  *p++=0xa0;
  SetSize(n,p);
  *p++=tcid;
  *p++=0x90;
  *p++=0x02;
  *p++=cid>>8;
  *p++=cid&0xff;
  *p++=0x9f;
  *p++=0x80;
  *p++=(unsigned char)AOT_CA_INFO;
  SetSize(cn,p);
  for(int i=0; caids[i]; i++)
  {
	  *p++=caids[i]>>8;
	  *p++=caids[i]&0xff;
  }
  frame.Put();
  isyslog("DVBAPI: SCCAMSlot::CaInfo %i.%i sending CA info",cardIndex,slot);
}

void SCCAMSlot::Process(const unsigned char *data, int len)
{

  const unsigned char *save=data;
  data+=3;
  int dlen=GetLength(data);
  if(dlen>len-(data-save)) {
    isyslog("DVBAPI: SCCAMSlot::Process %d.%d TDPU length exceeds data length",cardIndex,slot);
    dlen=len-(data-save);
    }
  int tcid=data[0];
//  isyslog("DVBAPI: SCCAMSlot::Process len=%d tcid=%i",len,tcid);

  if(Check())
	 CaInfo(tcid,0x01);

  if(dlen<8 || data[1]!=0x90)
	  return;
  int cid=(data[3]<<8)+data[4];
  int tag=(data[5]<<16)+(data[6]<<8)+data[7];
  data+=8;
  dlen=GetLength(data);
  if(dlen>len-(data-save)) {
	  isyslog("DVBAPI: SCCAMSlot::Process %d.%d tag length exceeds data length",cardIndex,slot);
    dlen=len-(data-save);
    }
  switch(tag) {
    case AOT_CA_INFO_ENQ:
      CaInfo(tcid,cid);
      break;

    case AOT_CA_PMT:
      if(dlen>=6) {
        int ca_lm=data[0];
        int ci_cmd=-1;
        int sid=(data[1]<<8)+data[2];
        int ilen=(data[4]<<8)+data[5];
        isyslog("DVBAPI: SCCAMSlot::Process %d.%d CA_PMT decoding len=%x lm=%x prg=%d len=%x",cardIndex,slot,dlen,ca_lm,sid,ilen);
        data+=6; dlen-=6;
        if(ilen>0 && dlen>=ilen) {
          ci_cmd=data[0];
          }
        data+=ilen; dlen-=ilen;
        while(dlen>=5) {
          ilen=(data[3]<<8)+data[4];
          data+=5; dlen-=5;
          if(ilen>0 && dlen>=ilen) {
            ci_cmd=data[0];
            }
          data+=ilen; dlen-=ilen;
          }
        isyslog("DVBAPI: SCCAMSlot::Process %d.%d got CA pmt ciCmd=%d caLm=%d",cardIndex,slot,ci_cmd,ca_lm);
        if(doReply && (ci_cmd==0x03 || (ci_cmd==0x01 && ca_lm==0x03))) {
          unsigned char *b;
          if((b=frame.GetBuff(4+11))) {
            b[0]=0xa0; b[2]=tcid;
            b[3]=0x90;
            b[4]=0x02; b[5]=cid<<8; b[6]=cid&0xff;
            b[7]=0x9f; b[8]=0x80; b[9]=0x33; // AOT_CA_PMT_REPLY
            b[11]=sid<<8;
            b[12]=sid&0xff;
            b[13]=0x00;
            b[14]=0x81; 	// CA_ENABLE
            b[10]=4; b[1]=4+9;
            frame.Put();
            isyslog("DVBAPI: SCCAMSlot::Process %d.%d answer to query",cardIndex,slot);
            }
          }
        if(sid!=0) {
          if(ci_cmd==0x04) {
            isyslog("DVBAPI: SCCAMSlot::Process %d.%d stop decrypt",cardIndex,slot);
            }
          if(ci_cmd==0x01 || (ci_cmd==-1 && (ca_lm==0x04 || ca_lm==0x05))) {
            isyslog("DVBAPI: SCCAMSlot::Process %d.%d set CAM decrypt (SID %d)",cardIndex,slot,sid);

	    SCDVBDevice *dev=dynamic_cast<SCDVBDevice *>(Device());
	    if(dev)
		dev->GetSCCIAdapter()->ProcessSIDRequest(Device()->DeviceNumber(), sid, ca_lm);
	    else
		esyslog("DVBAPI: SCCAMSlot::Process %d.%d FATAL ERROR: cannot find CIAdapter for ProcessSIDRequest", cardIndex, slot);
            }
          }
        }
      break;
    }
 }
