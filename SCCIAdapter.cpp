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

#define SOCKET_CHECK_INTERVAL   3000

// from vdr's ci.c
#define T_CREATE_TC    0x82
#define T_RCV          0x81
#define T_DATA_LAST    0xA0

SCCIAdapter::SCCIAdapter(cDevice *Device, int cardIndex, int cafd, bool SoftCSA, bool FullTS)
{
  this->cardIndex = cardIndex;
  device = Device;
  capmt = new CAPMT;
  fd_ca = cafd;
  softcsa = SoftCSA;
  fullts = FullTS;

  decsa = softcsa ? new DeCSA(cardIndex) : 0;
  UDPSocket::bindx(this);

  memset(version, 1, sizeof(version));
  memset(slots, 0, sizeof(slots));
  memset(caids, 0, sizeof(caids));
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

  if (checkTimer.TimedOut())
  {
    OSCamCheck();
    checkTimer.Set(SOCKET_CHECK_INTERVAL);
  }
}

void SCCIAdapter::OSCamCheck()
{
  /* temporarily disabled, will be done differently after rework
  for (int i = 0; i < MAX_SOCKETS; i++)
  {
    if (sockets[i] != 0)
    {
      DEBUGLOG("%s: %d: checking if connection to socket_fd=%d is still alive", __FUNCTION__, cardIndex, sockets[i]);
      if ((sockets[i] == -1) || (write(sockets[i], NULL, 0) < 0))
      {
        if (sids[i] != 0)    //we have a SID assigned with this socket, need to reconnect and resend PMT data
        {
          int new_fd = capmt->send(cardIndex, sids[i], -1, NULL, 0);
          if (new_fd > 0)
          {
            INFOLOG("%d: reconnected successfully, replacing socket_fd %d with %d", cardIndex, sockets[i], new_fd);
            sockets[i] = new_fd;
          }
        }
        else                 //no SID assigned to this socket, mark as invalid
          sockets[i] = 0;
      }
    }
  }*/
  return;
}

SCCIAdapter::~SCCIAdapter()
{
  DEBUGLOG("%s", __FUNCTION__);

  Cancel(3);

  ciMutex.Lock();
  delete rb;
  rb = 0;
  ciMutex.Unlock();

  if (decsa)
    delete decsa;
  if (capmt != 0)
    delete capmt;
  capmt = 0;
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
  return Device ? (Device == device) : true;
}

bool SCCIAdapter::DeCSASetCaDescr(ca_descr_t *ca_descr)
{
  DEBUGLOG("%s: index=%d", __FUNCTION__, ca_descr->index);
  if (!softcsa || (fullts && ca_descr->index == 0))
  {
    cMutexLock lock(&cafdMutex);
    return ioctl(fd_ca, CA_SET_DESCR, ca_descr) >= 0;
  }
  if (ca_descr->index == (unsigned) -1)
  {
    DEBUGLOG("%s: removal request - ignoring", __FUNCTION__);
    return true;
  }
  bool ret = decsa->SetDescr(ca_descr, true);
  //initialCaDscr = false;
  return ret;
}

bool SCCIAdapter::DeCSASetCaPid(ca_pid_t *ca_pid)
{
  DEBUGLOG("%s: PID=%d, index=%d", __FUNCTION__, ca_pid->pid, ca_pid->index);
  if (!softcsa || (fullts && ca_pid->index == 0))
  {
    cMutexLock lock(&cafdMutex);
    return ioctl(fd_ca, CA_SET_PID, ca_pid) >= 0;
  }
  if (ca_pid->index == -1)
  {
    DEBUGLOG("%s: removal request - ignoring", __FUNCTION__);
    return true;
  }
  return decsa->SetCaPid(ca_pid);
}
