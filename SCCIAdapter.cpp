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
#include "Frame.h"
#include "Log.h"

// from vdr's ci.c
#define T_CREATE_TC    0x82
#define T_RCV          0x81
#define T_DATA_LAST    0xA0

SCCIAdapter::SCCIAdapter(SCDVBDevice *sCDVBDevice, int cardIndex)
{
  for (int i = 0; i < MAX_SOCKETS; i++)
  {
    sids[i] = 0;
    sockets[i] = 0;
  }

  this->sCDVBDevice = sCDVBDevice;
  this->cardIndex = cardIndex;
  memset(version, 1, sizeof(version));
  memset(slots, 0, sizeof(slots));
  caidsLength = 0;
  Channels.Lock(false);
  for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel))
  {
    if (!channel->GroupSep() && channel->Ca() >= CA_ENCRYPTED_MIN)
    {
      for (const int *ids = channel->Caids(); *ids; ids++)
        addCaid(0, caidsLength, (unsigned short) *ids);
    }
  }
  Channels.Unlock();
  rb = new cRingBufferLinear(KILOBYTE(8), 6 + LEN_OFF, false, "SC-CI adapter read");
  if (rb)
  {
    rb->SetTimeouts(0, CAM_READ_TIMEOUT);
    frame.SetRb(rb);
  }
  INFOLOG("%s: built caid table with %i caids for %i channels", __FUNCTION__, caidsLength, Channels.Count());
  SetDescription("SC-CI adapter on device %d", cardIndex);
  for (int i = 0; i < MAX_CI_SLOTS && i * MAX_CI_SLOT_CAIDS < caidsLength; i++)
    slots[i] = new SCCAMSlot(this, cardIndex, i);
  Start();
}

SCDVBDevice *SCCIAdapter::GetDevice()
{
  return sCDVBDevice;
}

int SCCIAdapter::addCaid(int offset, int limit, unsigned short caid)
{
  if ((caids[offset] == caid))
    return offset;
  if ((limit == 0) || (caidsLength == 0))
  {
    if ((caid > caids[offset]) && (offset < caidsLength))
      offset++;
    if (offset < caidsLength)
      memmove(&caids[offset + 1], &caids[offset], (caidsLength - offset) * sizeof(int));
    caidsLength++;
    caids[offset] = caid;
    return offset;
  }
  if (caid > caids[offset])
  {
    offset = offset + limit;
    if (offset > caidsLength)
      offset = caidsLength;
  }
  else if (caid < caids[offset])
  {
    offset = offset - limit;
    if (offset < 0)
      offset = 0;
  }
  if (limit == 1)
    limit = 0;
  else
    limit = ceil(((float) limit) / 2.0f);
  if (offset + limit > caidsLength)
    offset = caidsLength - limit;
  return addCaid(offset, limit, caid);
}

int SCCIAdapter::GetCaids(int slot, unsigned short *Caids, int max)
{
  cMutexLock lock(&ciMutex);
  if (Caids)
  {
    int i;
    for (i = 0; i < MAX_CI_SLOT_CAIDS && i < max && slot * MAX_CI_SLOT_CAIDS + i < caidsLength && caids[slot * MAX_CI_SLOT_CAIDS + i]; i++)
      Caids[i] = caids[slot * MAX_CI_SLOT_CAIDS + i];
    Caids[i] = 0;
  }
  return version[slot];
}

int SCCIAdapter::Read(unsigned char *Buffer, int MaxLength)
{
  cMutexLock lock(&ciMutex);
  if (rb && Buffer && MaxLength > 0)
  {
    int s;
    unsigned char *data = frame.Get(s);
    if (data)
    {
      if (s <= MaxLength)
        memcpy(Buffer, data, s);
      else
        ERRORLOG("internal: sc-ci %d rb frame size exceeded %d", cardIndex, s);
      frame.Del();
      if (Buffer[2] != 0x80)
        readTimer.Set();
      return s;
    }
  }
  else
    cCondWait::SleepMs(CAM_READ_TIMEOUT);
  if (readTimer.Elapsed() > 2000)
    readTimer.Set();
  return 0;
}

#define TPDU(data,slot)   do { unsigned char *_d=(data); _d[0]=(slot); _d[1]=tcid; } while(0)
#define TAG(data,tag,len) do { unsigned char *_d=(data); _d[0]=(tag); _d[1]=(len); } while(0)
#define SB_TAG(data,sb)   do { unsigned char *_d=(data); _d[0]=0x80; _d[1]=0x02; _d[2]=tcid; _d[3]=(sb); } while(0)

void SCCIAdapter::Write(const unsigned char *buff, int len)
{
  cMutexLock lock(&ciMutex);
  if (buff && len >= 5)
  {
    struct TPDU *tpdu = (struct TPDU *) buff;
    int slot = tpdu->slot;
    if (slots[slot])
    {
      Frame *slotframe = slots[slot]->getFrame();
      switch (tpdu->tag)
      {
      case T_RCV:
        {
          int s;
          unsigned char *d = slotframe->Get(s);
          if (d)
          {
            unsigned char *b;
            if ((b = frame.GetBuff(s + 6)))
            {
              TPDU(b, slot);
              memcpy(b + 2, d, s);
              slotframe->Del(); // delete from rb before Avail()
              SB_TAG(b + 2 + s, slotframe->Avail() > 0 ? 0x80 : 0x00);
              frame.Put();
            }
            else
              slotframe->Del();
          }
          break;
        }
      case T_CREATE_TC:
        {
          tcid = tpdu->data[0];
          unsigned char *b;
          static const unsigned char reqCAS[] = { 0xA0, 0x07, 0x01, 0x91, 0x04, 0x00, 0x03, 0x00, 0x41 };
          if ((b = slotframe->GetBuff(sizeof(reqCAS))))
          {
            memcpy(b, reqCAS, sizeof(reqCAS));
            b[2] = tcid;
            slotframe->Put();
          }
          if ((b = frame.GetBuff(9)))
          {
            TPDU(b, slot);
            TAG(&b[2], 0x83, 0x01);
            b[4] = tcid;
            SB_TAG(&b[5], slotframe->Avail() > 0 ? 0x80 : 0x00);
            frame.Put();
          }
          break;
        }
      case T_DATA_LAST:
        {
          slots[slot]->Process(buff, len);
          unsigned char *b;
          if ((b = frame.GetBuff(6)))
          {
            TPDU(b, slot);
            SB_TAG(&b[2], slotframe->Avail() > 0 ? 0x80 : 0x00);
            frame.Put();
          }
          break;
        }
      }
    }
  }
  else
    DEBUGLOG("%d: short write (buff=%d len=%d)", cardIndex, buff != 0, len);
}

SCCIAdapter::~SCCIAdapter()
{
  DEBUGLOG("%s", __FUNCTION__);

  Cancel(3);

  for (int i = 0; i < MAX_SOCKETS; i++)
  {
    if (sockets[i] != 0)
    {
      close(sockets[i]);
      sockets[i] = 0;
    }
  }
  ciMutex.Lock();
  delete rb;
  rb = 0;
  ciMutex.Unlock();
}


bool SCCIAdapter::Ready(void)
{
  return true;
}

bool SCCIAdapter::Reset(int Slot)
{
  cMutexLock lock(&ciMutex);
  return true;
  //return slots[Slot]->Reset();
}

eModuleStatus SCCIAdapter::ModuleStatus(int Slot)
{
  cMutexLock lock(&ciMutex);
  return (slots[Slot]) ? slots[Slot]->Status() : msNone;
}

bool SCCIAdapter::Assign(cDevice *Device, bool Query)
{
  return true;
}

void SCCIAdapter::ProcessSIDRequest(int card_index, int sid, int ca_lm, const unsigned char *vdr_caPMT, int vdr_caPMTLen)
{
/*
    here is what i found so far analyzing AOT_CA_PMT frame from vdr
    lm=4 prg=X: request for X SID to be decrypted (added)
    lm=5 prg=X: request for X SID to stop decryption (removed)
    lm=3 prg=0: this is sent when changing transponder or during epg scan (also before new channel but ONLY if it is on different transponder)
    lm=3 prg=X: it seems that this is sent when starting vdr with active timers
*/
  int i;

  //for (i = 0; i < MAX_SOCKETS; i++)
    //DEBUGLOG("%s: SOCKETS TABLE DUMP [%d]: sid=%d socket=%d", __FUNCTION__, i, sids[i], sockets[i]);
  if (sid == 0)
  {
    DEBUGLOG("%s: got empty SID - returning from function", __FUNCTION__);
    return;
  }
  if (ca_lm == 0x04 || ca_lm == 0x03)   //adding new sid
  {
    int found = 0;
    for (i = 0; i < MAX_SOCKETS; i++)
    {
      if (sids[i] == sid)
      {
        found = 1;
        break;
      }
    }

    if (found)
      DEBUGLOG("%s: found sid, reusing socket, i=%d", __FUNCTION__, i);
    else                        //not found - adding to first free in table
    {
      for (i = 0; i < MAX_SOCKETS; i++)
      {
        if (sids[i] == 0)
        {
          sids[i] = sid;
          break;
        }
      }
    }
    if (i == MAX_SOCKETS)
    {
      ERRORLOG("%s: no free space for new SID!!!", __FUNCTION__);
      return;
    }
    else
    {
      sids[i] = sid;
      DEBUGLOG("%s: added: i=%d", __FUNCTION__, i);
    }
  }
  else if (ca_lm == 0x05)       //removing sid
  {
    for (i = 0; i < MAX_SOCKETS; i++)
    {
      if (sids[i] == sid)
        break;
    }
    if (i == MAX_SOCKETS)
    {
      ERRORLOG("%s: socket to close not found", __FUNCTION__);
      return;
    }

    //closing socket (oscam handles this as event and stop decrypting)
    DEBUGLOG("%s: closing socket i=%d, socket_fd=%d", __FUNCTION__, i, sockets[i]);
    sids[i] = 0;
    if (sockets[i] > 0)
      close(sockets[i]);
    sockets[i] = 0;
    return;
  }
  else
  {
    ERRORLOG("%s: unhandled ca_lm request type = %d", __FUNCTION__, ca_lm);
    return;
  }

  sockets[i] = GetDevice()->GetCAPMT()->send(card_index, sid, sockets[i], vdr_caPMT, vdr_caPMTLen);
  GetDevice()->SetReady(true);
}
