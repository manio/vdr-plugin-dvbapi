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

#include "CAPMT.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include "Log.h"
#include "DVBAPI.h"

CAPMT *capmt = NULL;

CAPMT::CAPMT()
{
  memset(&caPMT, 0, sizeof(caPMT));
}

CAPMT::~CAPMT()
{
  cMutexLock lock(&mutex);
  vector<pmtobj>::iterator it;
  for (it=pmt.begin(); it!=pmt.end(); ++it)
  {
    if (it->data)
      delete[] it->data;
  }
  pmt.clear();
}

void CAPMT::ProcessSIDRequest(int card_index, int sid, int ca_lm, const unsigned char *vdr_caPMT, int vdr_caPMTLen)
{
/*
    here is what i found so far analyzing AOT_CA_PMT frame from vdr
    lm=4 prg=X: request for X SID to be decrypted (added)
    lm=5 prg=X: request for X SID to stop decryption (removed)
    lm=3 prg=0: this is sent when changing transponder or during epg scan (also before new channel but ONLY if it is on different transponder)
    lm=3 prg=X: it seems that this is sent when starting vdr with active timers
*/
  cMutexLock lock(&mutex);
  bool removed = false;
  if (sid == 0)
  {
    DEBUGLOG("%s: got empty SID - returning from function", __FUNCTION__);
    return;
  }

  //removind the PMT if exists
  vector<pmtobj>::iterator it;
  for (it = pmt.begin(); it != pmt.end(); ++it)
  {
    if (it->sid == sid && it->adapter == card_index)
    {
      if (it->data)
        delete[] it->data;
      pmt.erase(it);
      removed = true;
      break;
    }
  }
  //nothing to update/remove
  if (ca_lm == 0x05 && !removed && !vdr_caPMTLen)
    return;
  //adding new or updating existing PMT data
  if (ca_lm == 0x04 || ca_lm == 0x03)
  {
    pmtobj pmto;
    pmto.sid = sid;
    pmto.len = vdr_caPMTLen;
    pmto.adapter = card_index;
    if (vdr_caPMTLen > 0)
    {
      int length;
      int offset = 0;

      pmto.pilen[0] = vdr_caPMT[4];   //reserved+program_info_length
      pmto.pilen[1] = vdr_caPMT[5];   //reserved+program_info_length (+1 for ca_pmt_cmd_id, +4 for above CAPMT_DESC_DEMUX)

      //obtaining program_info_length
      int ilen = (vdr_caPMT[4] << 8) + vdr_caPMT[5];
      //checking if we need to start copying 1 byte further and omit ca_pmt_cmd_id which we already have
      if (ilen > 0)
      {
        offset = 1;
        pmto.pilen[1] -= 1;           //-1 for ca_pmt_cmd_id which we have counted
      }

      length = vdr_caPMTLen;      //ca_pmt data length
      pmto.len = length - 6 - offset;
      unsigned char *pmt_data = new unsigned char[pmto.len];
      memcpy(pmt_data, vdr_caPMT + 6 + offset, pmto.len);    //copy ca_pmt data from vdr
      pmto.data = pmt_data;
    }
    else
    {
      DEBUGLOG("CA_PMT doesn't contain CA descriptors");
      return;
    }

    pmt.push_back(pmto);
  }

  SendAll();
}

void CAPMT::SendAll()
{
  cMutexLock lock(&mutex);
  vector<pmtobj>::iterator it;

  if (pmt.empty())
    SockHandler->SendStopDescrambling();
  else
  {
    //sending complete PMT objects
    int lm = LIST_FIRST;
    for (it = pmt.begin(); it != pmt.end();)
    {
      int sid = it->sid;
      pmtobj *pmto = &*it;
      ++it;
      if (it == pmt.end())
        lm |= LIST_LAST;
      send(pmto->adapter, sid, lm, pmto);
      lm = LIST_MORE;
    }
  }
}

void CAPMT::send(const int adapter, const int sid, int ca_lm, const pmtobj *pmt)
{
  int length_field;
  int toWrite;

/////// preparing capmt data to send
  // http://cvs.tuxbox.org/lists/tuxbox-cvs-0208/msg00434.html
  DEBUGLOG("%s: channelSid=0x%x (%d)", __FUNCTION__, sid, sid);

  //ca_pmt_tag
  caPMT[0] = 0x9F;
  caPMT[1] = 0x80;
  caPMT[2] = 0x32;
  caPMT[3] = 0x82;              //2 following bytes for size

  caPMT[6] = ca_lm;             //list management
  caPMT[7] = sid >> 8;          //program_number
  caPMT[8] = sid & 0xff;        //program_number
  caPMT[9] = 0;                 //version_number, current_next_indicator

  caPMT[12] = 0x01;             //ca_pmt_cmd_id = CAPMT_CMD_OK_DESCRAMBLING
  //adding own descriptor with demux and adapter_id
  caPMT[13] = 0x83;             //adapter_device_descriptor
  caPMT[14] = 0x01;             //length
  caPMT[15] = (char) adapter + AdapterIndexOffset;   //adapter id

  caPMT[16] = 0x86;             //demux_device_descriptor
  caPMT[17] = 0x01;             //length
  caPMT[18] = 0x00;             //demux id

  caPMT[19] = 0x87;             //ca_device_descriptor
  caPMT[20] = 0x01;             //length
  caPMT[21] = (char) adapter + AdapterIndexOffset;   //ca id

  //adding CA_PMT from vdr
  caPMT[10] = pmt->pilen[0];                  //reserved+program_info_length
  caPMT[11] = pmt->pilen[1] + 1 + 3 + 3 + 3;  //reserved+program_info_length (+1 for ca_pmt_cmd_id, +3 +3 +3 are three descriptors)
  memcpy(caPMT + 22, pmt->data, pmt->len);    //copy ca_pmt data from vdr
  length_field = 22 + pmt->len - 6;           //-6 = 3 bytes for AOT_CA_PMT and 3 for size

  //calculating length_field()
  caPMT[4] = length_field >> 8;
  caPMT[5] = length_field & 0xff;

  //number of bytes in packet to send (adding 3 bytes of ca_pmt_tag and 3 bytes of length_field)
  toWrite = length_field + 6;

  //sending data
  SockHandler->Write(caPMT, toWrite);
}

void CAPMT::UpdateEcmInfo(int adapter_index, int sid, uint16_t caid, uint16_t pid, uint32_t prid, uint32_t ecmtime, char *cardsystem, char *reader, char *from, char *protocol, int8_t hops)
{
  cMutexLock lock(&mutex);
  vector<pmtobj>::iterator it;

  if (!pmt.empty())
  {
    for (it = pmt.begin(); it != pmt.end(); ++it)
    {
      if (it->sid == sid && it->adapter == adapter_index)
      {
        DEBUGLOG("%s: PMTO update, adapter=%d, SID=%04X", __FUNCTION__, adapter_index, sid);
        it->caid = caid;
        it->pid = pid;
        it->prid = prid;
        it->ecmtime = ecmtime;
        it->cardsystem = cardsystem;
        it->reader = reader;
        it->from = from;
        it->protocol = protocol;
        it->hops = hops;
        break;
      }
    }
  }
}

bool CAPMT::FillEcmInfo(sDVBAPIEcmInfo *ecminfo)
{
  cMutexLock lock(&mutex);
  vector<pmtobj>::iterator it;

  if (!pmt.empty())
  {
    for (it = pmt.begin(); it != pmt.end(); ++it)
    {
      if (it->sid == ecminfo->sid)
      {
        DEBUGLOG("%s: PMTO match - fill, adapter=%d, SID=%04X", __FUNCTION__, it->adapter, it->sid);
        ecminfo->caid = it->caid;
        ecminfo->pid = it->pid;
        ecminfo->prid = it->prid;
        ecminfo->ecmtime = it->ecmtime;
        ecminfo->cardsystem = it->cardsystem;
        ecminfo->reader = it->reader;
        ecminfo->from = it->from;
        ecminfo->protocol = it->protocol;
        ecminfo->hops = it->hops;
        return true;
      }
    }
  }
  return false;
}
