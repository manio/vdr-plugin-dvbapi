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
#include <vdr/ci.h>
#include <vdr/thread.h>
#include "SCCAMSlot.h"
#include "Log.h"

// from vdr's ci.c
#define AOT_CA_INFO_ENQ             0x9F8030
#define AOT_CA_INFO                 0x9F8031
#define AOT_CA_PMT                  0x9f8032

SCCAMSlot::SCCAMSlot(SCCIAdapter *sCCIAdapter, int cardIndex, int slot, cCamSlot *MasterSlot)
 : cCamSlot(sCCIAdapter, true, MasterSlot)
 , checkTimer(-SLOT_CAID_CHECK - 1000)
 , rb(KILOBYTE(4), 5 + LEN_OFF, false, "SC-CI slot answer")
 , decsaFillControl(200000, 100, 40)
{
  this->sCCIAdapter = sCCIAdapter;
  this->cardIndex = cardIndex;
  this->slot = slot;
  version = 0;
  doReply = false;
  lastStatus = msReset;
  frame.SetRb(&rb);
  ResetSlot(false);
}

eModuleStatus SCCAMSlot::Status(void)
{
  eModuleStatus status;
  if (reset)
  {
    status = msReset;
    reset = false;
  }
  else if (version)
    status = msReady;
  else
  {
    status = msPresent;         //msNone;
    Check();
  }
  if (status != lastStatus)
  {
    static const char *stext[] = { "none", "reset", "present", "ready" };
    INFOLOG("%d.%d: status '%s'", cardIndex, slot, stext[status]);
    lastStatus = status;
  }
  return status;
}

bool SCCAMSlot::ResetSlot(bool log)
{
  DEBUGLOG("%s: log=%i", __FUNCTION__, log);
  reset = true;
  rb.Clear();
  if (log)
    INFOLOG("%d.%d: reset", cardIndex, slot);
  return reset;
}

bool SCCAMSlot::Check(void)
{
  bool res = false;
  bool dr = true;
  //bool dr = ciadapter->CamSoftCSA() || ScSetup.ConcurrentFF>0;
  if (dr != doReply && !IsDecrypting())
  {
    INFOLOG("%d.%d: doReply changed, reset triggered", cardIndex, slot);
    ResetSlot(false);
    doReply = dr;
  }
  if (checkTimer.TimedOut())
  {
    if (version != sCCIAdapter->GetVersion())
    {
      version = sCCIAdapter->GetVersion();
      INFOLOG("%d.%d: now using CAIDs version %d", cardIndex, slot, version);
      res = true;
    }
    checkTimer.Set(SLOT_CAID_CHECK);
  }
  return res;
}

bool SCCAMSlot::Assign(cDevice *Device, bool Query)
{
  if (!Device || Device->CardIndex() == cardIndex)
    return cCamSlot::Assign(Device, Query);
  return false;
}

const char *SCCAMSlot::GetCamName(void)
{
  return "OSCam";
}

bool SCCAMSlot::ProvidesCa(const int *CaSystemIds)
{
  //assume OSCam is able to decrypt this CAID
  return true;
}

int SCCAMSlot::GetLength(const unsigned char *&data)
{
  int len = *data++;
  if (len & TDPU_SIZE_INDICATOR)
  {
    int i;
    for (i = len & ~TDPU_SIZE_INDICATOR, len = 0; i > 0; i--)
      len = (len << 8) + *data++;
  }
  return len;
}

uchar *SCCAMSlot::Decrypt(uchar *Data, int &Count)
{
  if (!Data)
    return NULL;
  if (!decsaFillControl.CanProcess(Data, Count))
  {
    Count = 0;
    return NULL;
  }
  if (Data[3] & TS_SCRAMBLING_CONTROL)
    decsa->Decrypt(cardIndex, Data, Count, true);
  else
    filter->Analyze(cardIndex, Data, Count);
  Count = TS_SIZE;
  return Data;
}

int SCCAMSlot::LengthSize(int n)
{
  return n < TDPU_SIZE_INDICATOR ? 1 : 3;
}

void SCCAMSlot::SetSize(int n, unsigned char *&p)
{
  if (n < TDPU_SIZE_INDICATOR)
    *p++ = n;
  else
  {
    *p++ = 2 | TDPU_SIZE_INDICATOR;
    *p++ = n >> 8;
    *p++ = n & 0xFF;
  }
}

void SCCAMSlot::CaInfo(int tcid, int cid)
{
  int cn = 2;
  int n = cn + 8 + LengthSize(cn);
  unsigned char *p;
  if (!(p = frame.GetBuff(n + 1 + LengthSize(n))))
    return;
  *p++ = 0xa0;
  SetSize(n, p);
  *p++ = tcid;
  *p++ = 0x90;
  *p++ = 0x02;
  *p++ = cid >> 8;
  *p++ = cid & 0xff;
  *p++ = 0x9f;
  *p++ = 0x80;
  *p++ = (unsigned char) AOT_CA_INFO;
  SetSize(cn, p);
  //pass a 'wildcard' CAID to vdr
  *p++ = 0xff;
  *p++ = 0xff;
  frame.Put();
  INFOLOG("%s: %i.%i sending CA info", __FUNCTION__, cardIndex, slot);
}

void SCCAMSlot::Process(const unsigned char *data, int len)
{
  const unsigned char *save = data;

  data += 3;
  int dlen = GetLength(data);
  if (dlen > len - (data - save))
  {
    ERRORLOG("%d.%d TDPU length exceeds data length", cardIndex, slot);
    dlen = len - (data - save);
  }
  int tcid = data[0];

  if (Check())
    CaInfo(tcid, 0x01);

  if (dlen < 8 || data[1] != 0x90)
    return;
  int cid = (data[3] << 8) + data[4];
  int tag = (data[5] << 16) + (data[6] << 8) + data[7];
  data += 8;
  dlen = GetLength(data);
  if (dlen > len - (data - save))
  {
    ERRORLOG("%d.%d tag length exceeds data length", cardIndex, slot);
    dlen = len - (data - save);
  }
  switch (tag)
  {
  case AOT_CA_INFO_ENQ:
    CaInfo(tcid, cid);
    break;

  case AOT_CA_PMT:
    if (dlen >= 6)
    {
      bool HasCaDescriptors = false;
      const unsigned char *vdr_caPMT = data;
      int vdr_caPMTLen = dlen;

      int ca_lm = data[0];                      // lm -> list manager
      int ci_cmd = -1;
      int sid = (data[1] << 8) + data[2];       // program number
      int ilen = (data[4] << 8) + data[5];      // program info length
      DEBUGLOG("%d.%d CA_PMT decoding len=%x lm=%x prg=%d len=%x", cardIndex, slot, dlen, ca_lm, sid, ilen);
      data += 6;
      dlen -= 6;
      if (ilen > 0 && dlen >= ilen)
      {
        ci_cmd = data[0];
        if (ilen > 1)
          HasCaDescriptors = true;
        DEBUGLOG("ci_cmd(G)=%02x", ci_cmd);
      }
      data += ilen;
      dlen -= ilen;
      while (dlen >= 5)
      {
        ilen = (data[3] << 8) + data[4];        // ES_Info_length
        DEBUGLOG("pid=%d,%04x len=%d (0x%x)", data[0], (data[1] << 8) + data[2], ilen, ilen);
        data += 5;
        dlen -= 5;
        if (ilen > 0 && dlen >= ilen)
        {
          ci_cmd = data[0];
          if (ilen > 1)
            HasCaDescriptors = true;
          DEBUGLOG("ci_cmd(S)=%02x", ci_cmd);
        }
        data += ilen;
        dlen -= ilen;
      }
      DEBUGLOG("%d.%d got CA pmt ciCmd=%d caLm=%d", cardIndex, slot, ci_cmd, ca_lm);
      if (doReply && (ci_cmd == 0x03 || (ci_cmd == 0x01 && ca_lm == 0x03)))
      {
        unsigned char *b;
        if ((b = frame.GetBuff(4 + 11)))
        {
          b[0] = 0xa0;
          b[2] = tcid;
          b[3] = 0x90;
          b[4] = 0x02;
          b[5] = cid << 8;
          b[6] = cid & 0xff;
          b[7] = 0x9f;
          b[8] = 0x80;
          b[9] = 0x33;          // AOT_CA_PMT_REPLY
          b[11] = sid << 8;
          b[12] = sid & 0xff;
          b[13] = 0x00;
          b[14] = 0x81;         // CA_ENABLE
          b[10] = 4;
          b[1] = 4 + 9;
          frame.Put();
          DEBUGLOG("%d.%d answer to query", cardIndex, slot);
        }
      }
      else
        DEBUGLOG("%d.%d answer to query suppressed", cardIndex, slot);

      if (ci_cmd == 0x04 || (ci_cmd == -1 && sid == 0 && ca_lm == 0x03))
      {
        DEBUGLOG("%d.%d stop decrypt", cardIndex, slot);
        capmt->ProcessSIDRequest(cardIndex, sid, ca_lm, NULL, 0);
        if (decsa)
          decsa->Init_Parity(cardIndex, sid, -1, true);
      }
      else if (ci_cmd == 0x01 || (ci_cmd == -1 && sid != 0 && (ca_lm == 0x03 || ca_lm == 0x04 || ca_lm == 0x05)))
      {
        INFOLOG("%d.%d set CAM decrypt (SID %d (0x%04X), caLm %d, HasCaDescriptors %d)", cardIndex, slot, sid, sid, ca_lm, HasCaDescriptors);
        if (decsa)
          decsa->Init_Parity(cardIndex, sid, -1, false);

        if (!HasCaDescriptors)
        {
          vdr_caPMT = NULL;
          vdr_caPMTLen = 0;
        }
        capmt->ProcessSIDRequest(cardIndex, sid, ca_lm, vdr_caPMT, vdr_caPMTLen);
      }
      else
        DEBUGLOG("%d.%d no action taken", cardIndex, slot);
    }
    break;
  }
}

void SCCAMSlot::StartDecrypting(void)
{
  cCamSlot::StartDecrypting();
  decsaFillControl.Reset();
}

void SCCAMSlot::StopDecrypting(void)
{
  if (decsa)
    decsa->CancelWait();

  cCamSlot::StopDecrypting();
}

DeCSAFillControl::DeCSAFillControl(int MaxWaterMark, int Timeout, int DataInterval)
{
  maxWaterMark = MaxWaterMark;
  timeout = Timeout;
  dataInterval = DataInterval;
  if (dataInterval > timeout)
    dataInterval = timeout;
  sleepInterval = 20;
  minWaterMark = 2 * TS_SIZE;
  Reset();
}

bool DeCSAFillControl::CanProcess(const uchar *Data, int Count)
{
  switch (state)
  {
    case READY:
      if (Count < lowWaterMark)
      {
        lastCount = Count;
        lastData = Data;
        state = SLEEP;
        timeSlept = 0;
        cCondWait::SleepMs(sleepInterval);
        return false;
      }
      return true;
    case SLEEP:
      timeSlept += sleepInterval;
      if (timeSlept >= dataInterval && Count == lastCount && Data == lastData)
      {
        // we are probably stuck at the end of the ringbuffer
        state = WRAP;
        return true;
      }
      if (timeSlept >= timeout || Count - lastCount > maxWaterMark)
      {
        lowWaterMark = Count - lastCount;
        if (lowWaterMark > maxWaterMark)
          lowWaterMark = maxWaterMark;
        if (lowWaterMark < minWaterMark)
          lowWaterMark = minWaterMark;
        lowWaterMark = Filter(lowWaterMark);
        state = READY;
      }
      cCondWait::SleepMs(sleepInterval);
      return false;
    case WRAP:
      // we are here in 2 cases:
      //   1. at the end of the ringbuffer
      //   2. if no data arrived from the device within dataInterval ms
      // check the end pointer
      if (Data + Count != lastData + lastCount)
      {
        // keep lowWaterMark
        state = READY;
        return false;
      }
      lastData = Data;
      lastCount = Count;
      return true;
  }
  return true;
}

int DeCSAFillControl::Filter(int Input)
{
  int ret;
  if (fltTap1 >= 0 && fltTap2 >= 0)
    // 3-tap median filter
    ret = std::min(std::max(std::min(Input, fltTap1), fltTap2), std::max(Input, fltTap1));
  else
    ret = Input;
  fltTap2 = fltTap1;
  fltTap1 = Input;
  return ret;
}

void DeCSAFillControl::Reset(void)
{
  state = READY;
  lowWaterMark = maxWaterMark;
  fltTap1 = fltTap2 = -1;
}

