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

#ifndef ___SCCAMSLOT_H
#define ___SCCAMSLOT_H

#include "SCCIAdapter.h"
#include "Frame.h"

#define SLOT_CAID_CHECK   10000
#define SLOT_RESET_TIME     600
#define MAX_CW_IDX           16
#define CAID_TIME        300000   // time between caid scans
#define TRIGGER_TIME      10000   // min. time between caid scan trigger
#define MAX_SOCKETS          16   // max sockets (simultaneus channels) per demux

#define MAX_CI_SLOT_CAIDS    64

class SCCIAdapter;

class SCCAMSlot : public cCamSlot
{
private:
  SCCIAdapter *sCCIAdapter;
  unsigned short caids[MAX_CI_SLOT_CAIDS + 1];
  int slot, cardIndex, version;
  cTimeMs checkTimer;
  bool reset, doReply;
  cTimeMs resetTimer;
  eModuleStatus lastStatus;
  cRingBufferLinear rb;
  Frame frame;

public:
  SCCAMSlot(SCCIAdapter *ca, int cardIndex, int slot);
  int GetLength(const unsigned char *&data);
  int LengthSize(int n);
  void SetSize(int n, unsigned char *&p);
  void CaInfo(int tcid, int cid);
  bool Check(void);
  void Process(const unsigned char *data, int len);
  eModuleStatus Status(void);
  bool Reset(bool log = true);
  Frame *getFrame(void)
  {
    return &frame;
  }
};

#endif // ___SCCAMSLOT_H
