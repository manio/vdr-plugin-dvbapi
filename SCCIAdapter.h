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

#ifndef ___SCCIADAPTER_H
#define ___SCCIADAPTER_H

#include "SCCAMSlot.h"
#include "Frame.h"
#include "CAPMT.h"
#include "DeCSA.h"
#include "UDPSocket.h"
#include <vector>

using namespace std;

struct pmtobj
{
  int sid;
  int len;
  unsigned char* data;
};

class SCCAMSlot;

#define LIST_MORE           0x00
#define LIST_FIRST          0x01
#define LIST_LAST           0x02
#define LIST_ONLY           0x03
#define LIST_ADD            0x04
#define LIST_UPDATE         0x05

#define MAX_CI_SLOTS        8
#define MAX_SPLIT_SID       16

#define TDPU_SIZE_INDICATOR 0x80

struct TPDU
{
  unsigned char slot;
  unsigned char tcid;
  unsigned char tag;
  unsigned char len;
  unsigned char data[1];
};

class SCCIAdapter : public cCiAdapter
{
private:
  DeCSA *decsa;
  CAPMT *capmt;
  vector<pmtobj> pmt;
  cDevice *device;
  bool softcsa, fullts;
  bool initialCaDscr;
  unsigned short caids[1024];
  int caidsLength;
  int cardIndex;
  cMutex ciMutex;
  cMutex cafdMutex;
  SCCAMSlot *slots[MAX_CI_SLOTS];
  int version[MAX_CI_SLOTS];
  int fd_ca;
  cTimeMs caidTimer, triggerTimer;
  int tcid;
  cTimeMs readTimer, writeTimer;
  Frame frame;
  cRingBufferLinear *rb;
  int sids[MAX_SOCKETS];
  int sockets[MAX_SOCKETS];
  int addCaid(int offset, int limit, unsigned short caid);
  cTimeMs checkTimer;
  void OSCamCheck();

public:
  SCCIAdapter(cDevice *Device, int CardIndex, int cafd, bool SoftCSA, bool FullTS);
  ~SCCIAdapter();
  int Adapter()
  {
    return cardIndex;
  }
  virtual int Read(unsigned char *Buffer, int MaxLength);
  virtual void Write(const unsigned char *Buffer, int Length);
  virtual bool Reset(int Slot);
  virtual eModuleStatus ModuleStatus(int Slot);
  virtual bool Assign(cDevice *Device, bool Query = false);
  int GetCaids(int slot, unsigned short *Caids, int max);
  DeCSA *GetDeCSA()
  {
    return decsa;
  }
  bool DeCSASetCaDescr(ca_descr_t *ca_descr);
  bool DeCSASetCaPid(ca_pid_t *ca_pid);
  void ProcessSIDRequest(int card_index, int sid, int ca_lm, const unsigned char *vdr_caPMT, int vdr_caPMTLen);
};

#endif // ___SCCIADAPTER_H
