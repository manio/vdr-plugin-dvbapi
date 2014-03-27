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
      DEBUGLOG("CA_PMT doesn't contain CA desriptors");
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
    SockHandler->CloseConnection();
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
  caPMT[13] = 0x82;             //CAPMT_DESC_DEMUX
  caPMT[14] = 0x02;             //length
  caPMT[15] = 0x00;             //demux id
  caPMT[16] = (char) adapter;   //adapter id

  //adding CA_PMT from vdr
  caPMT[10] = pmt->pilen[0];                  //reserved+program_info_length
  caPMT[11] = pmt->pilen[1] + 1 + 4;          //reserved+program_info_length (+1 for ca_pmt_cmd_id, +4 for above CAPMT_DESC_DEMUX)
  memcpy(caPMT + 17, pmt->data, pmt->len);    //copy ca_pmt data from vdr
  length_field = 17 + pmt->len - 6;           //-6 = 3 bytes for AOT_CA_PMT and 3 for size

  //calculating length_field()
  caPMT[4] = length_field >> 8;
  caPMT[5] = length_field & 0xff;

  //number of bytes in packet to send (adding 3 bytes of ca_pmt_tag and 3 bytes of length_field)
  toWrite = length_field + 6;

  //sending data
  SockHandler->Write(caPMT, toWrite);
}
