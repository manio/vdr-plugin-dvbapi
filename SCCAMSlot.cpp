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
#include "SCCIAdapter.h"
#include "SCCAMSlot.h"
#include "Log.h"

// from vdr's ci.c
#define AOT_CA_INFO_ENQ             0x9F8030
#define AOT_CA_INFO                 0x9F8031
#define AOT_CA_PMT                  0x9f8032

SCCAMSlot::SCCAMSlot(SCCIAdapter *sCCIAdapter, int cardIndex, int slot)
 : cCamSlot(sCCIAdapter)
 , checkTimer(-SLOT_CAID_CHECK - 1000)
 , rb(KILOBYTE(4), 5 + LEN_OFF, false, "SC-CI slot answer")
{
  this->sCCIAdapter = sCCIAdapter;
  this->cardIndex = cardIndex;
  this->slot = slot;
  version = 0;
  caids[0] = 0;
  doReply = false;
  lastStatus = msReset;
  frame.SetRb(&rb);
  Reset(false);
}

eModuleStatus SCCAMSlot::Status(void)
{
  eModuleStatus status;
  if (reset)
  {
    status = msReset;
    if (resetTimer.TimedOut())
      reset = false;
  }
  else if (caids[0])
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

bool SCCAMSlot::Reset(bool log)
{
  DEBUGLOG("%s: log=%i", __FUNCTION__, log);
  reset = true;
  resetTimer.Set(SLOT_RESET_TIME);
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
    Reset(false);
    doReply = dr;
  }
  if (checkTimer.TimedOut())
  {
    if (version != sCCIAdapter->GetCaids(slot, 0, 0))
    {
      version = sCCIAdapter->GetCaids(slot, caids, MAX_CI_SLOT_CAIDS);
      INFOLOG("%d.%d: now using CAIDs version %d", cardIndex, slot, version);
      res = true;
    }
    checkTimer.Set(SLOT_CAID_CHECK);
  }
  return res;
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
  int cn = 0;
  for (int i = 0; caids[i]; i++)
    cn += 2;
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
  for (int i = 0; caids[i]; i++)
  {
    *p++ = caids[i] >> 8;
    *p++ = caids[i] & 0xff;
  }
  frame.Put();
  INFOLOG("%s: %i.%i sending CA info", __FUNCTION__, cardIndex, slot);
}

void SCCAMSlot::Process(const unsigned char *data, int len)
{
  const unsigned char *save = data;
  const unsigned char *vdr_caPMT = NULL;
  int vdr_caPMTLen = 0;
  bool HasCaDescriptors = false;

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
      vdr_caPMT = data;
      vdr_caPMTLen = dlen;

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
      if (sid != 0)
      {
        if (ci_cmd == 0x04)
          INFOLOG("%d.%d stop decrypt", cardIndex, slot);
        if (ci_cmd == 0x01 || (ci_cmd == -1 && (ca_lm == 0x04 || ca_lm == 0x05)))
        {
          INFOLOG("%d.%d set CAM decrypt (SID %d, caLm %d, HasCaDescriptors %d)", cardIndex, slot, sid, ca_lm, HasCaDescriptors);

          if (!HasCaDescriptors)
          {
            vdr_caPMT = NULL;
            vdr_caPMTLen = 0;
          }
          sCCIAdapter->ProcessSIDRequest(sCCIAdapter->Adapter(), sid, ca_lm, vdr_caPMT, vdr_caPMTLen);
        }
      }
    }
    break;
  }
}
