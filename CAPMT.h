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

#ifndef ___CAPMT_H
#define ___CAPMT_H

#include <linux/dvb/ca.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>
#include <vector>
#include "SocketHandler.h"

#define CAPMT_BUFFER_SIZE    1024

#define MAX_SOCKETS          16   // max sockets (simultaneus channels) per demux
#define LIST_MORE            0x00
#define LIST_FIRST           0x01
#define LIST_LAST            0x02
#define LIST_ONLY            0x03
#define LIST_ADD             0x04
#define LIST_UPDATE          0x05

struct sDVBAPIEcmInfo;

using namespace std;

struct pmtobj
{
  int sid;
  int len;
  int adapter;
  char pilen[2];
  unsigned char* data;

  //ecminfo
  uint16_t caid;
  uint16_t pid;
  uint32_t prid;
  uint32_t ecmtime;
  cString reader;
  cString from;
  cString protocol;
  int8_t hops;
};

class CAPMT
{
private:
  cMutex mutex;
  unsigned char caPMT[CAPMT_BUFFER_SIZE];
  bool get_pmt(const int adapter, const int sid, unsigned char *buft);
  vector<pmtobj> pmt;

public:
  CAPMT();
  ~CAPMT();
  void send(const int adapter, const int sid, int ca_lm, const pmtobj *pmt);
  void ProcessSIDRequest(int card_index, int sid, int ca_lm, const unsigned char *vdr_caPMT, int vdr_caPMTLen);
  bool Empty()
  {
    cMutexLock lock(&mutex);
    return pmt.empty();
  }
  void SendAll();
  void UpdateEcmInfo(int adapter_index, int sid, uint16_t caid, uint16_t pid, uint32_t prid, uint32_t ecmtime, char *reader, char *from, char *protocol, int8_t hops);
  bool FillEcmInfo(sDVBAPIEcmInfo *ecminfo);
};

extern CAPMT *capmt;

#endif // ___CAPMT_H
