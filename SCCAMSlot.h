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

#include <vdr/dvbdevice.h>
#include "SCCIAdapter.h"
#include "Frame.h"
#include "Filter.h"

#define SLOT_CAID_CHECK   10000

// This class helps to use a parallel descrambler efficiently by ensuring
// that there are always enough packets to descramble. It is done by
// keeping a low-water mark that tracks TS bitrate and is updated with
// the number of bytes arrived during a timeout period. Descrambling is
// allowed only if there are at least low-water mark bytes in the
// input buffer (must be cRingBufferLinear based).
class DeCSAFillControl
{
private:
  int maxWaterMark, timeout, dataInterval;
  int minWaterMark;
  int lowWaterMark, lastCount;
  const uchar *lastData;
  enum {READY, SLEEP1, SLEEP2, WRAP} state;
  int fltTap1, fltTap2;
  int Filter(int Input);
public:
  DeCSAFillControl(int MaxWaterMark, int Timeout, int DataInterval);
  // MaxWaterMark  a reasonable limit. The more, the better; but consider
  //               the ring buffer size.
  // Timeout       wait timeout in ms; should be chosen so that the number
  //               of packets of a video stream with the lowest bitrate
  //               which arrive within this interval is roughly equal
  //               to the maximum batch size. The more, the better, but
  //               it is limited by the MaxWaterMark and is added to the
  //               zapping time. For example, 128 packets within 100 ms
  //               gives (128 * 188 * 8) / 0.1 = 1.9 Mbit/s.
  // DataInterval  an interval in ms within which at least one packet
  //               arrives from the device. If not known, should be
  //               set to Timeout.
  bool CanProcess(const uchar *Data, int Count);
  // Should be called on each cCamSlot::Decrypt(), on each packet (not only
  // encrypted). If it returns false, Decrypt() should return NULL and
  // set Count to 0.
  void Reset(void);
};

class cDvbapiFilter;
class SCCIAdapter;

class SCCAMSlot : public cCamSlot
{
private:
  SCCIAdapter *sCCIAdapter;
  int slot, cardIndex, version;
  cTimeMs checkTimer;
  bool reset, doReply;
  cTimeMs resetTimer;
  eModuleStatus lastStatus;
  cRingBufferLinear rb;
  Frame frame;
  DeCSAFillControl decsaFillControl;

public:
  SCCAMSlot(SCCIAdapter *ca, int cardIndex, int slot);
  int GetLength(const unsigned char *&data);
  int LengthSize(int n);
  void SetSize(int n, unsigned char *&p);
  void CaInfo(int tcid, int cid);
  bool Check(void);
  void Process(const unsigned char *data, int len);
  eModuleStatus Status(void);
  bool ResetSlot(bool log = true);
  Frame *getFrame(void)
  {
    return &frame;
  }
  uchar *Decrypt(uchar *Data, int &Count);
  virtual const char *GetCamName(void);
  bool ProvidesCa(const int *CaSystemIds);
  virtual void StartDecrypting(void);
};

#endif // ___SCCAMSLOT_H
