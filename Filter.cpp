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

#include "Filter.h"
#include "Log.h"

cDvbapiFilter *filter = NULL;

cDvbapiFilter::~cDvbapiFilter()
{
  StopAllFilters();
}

bool cDvbapiFilter::SetFilter(uint8_t adapter_index, int pid, int start, unsigned char demux, unsigned char num, unsigned char *filter, unsigned char *mask)
{
  cMutexLock lock(&mutex);
  if ((unsigned) pid < MAX_CSA_PID)
  {
    DEBUGLOG("%s: adapter=%d set FILTER pid=%04X start=%d, demux=%d, filter=%d", __FUNCTION__, adapter_index, pid, start, demux, num);
#ifndef LIBDVBCSA
    int inum = num;
    if (decsa && inum==1)
      decsa->SetDVBAPIPid(adapter_index, demux, pid);
#endif
    vector<dmxfilter>::iterator it;
    if (start == 1 && filter && mask)
    {
      char hexdump[DMX_FILTER_SIZE * 3];
      sprintf(hexdump, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", filter[0], filter[1], filter[2], filter[3], filter[4], filter[5], filter[6], filter[7], filter[8], filter[9], filter[10], filter[11], filter[12], filter[13], filter[14], filter[15]);
      DEBUGLOG("   --> FILTER: %s", hexdump);
      sprintf(hexdump, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", mask[0], mask[1], mask[2], mask[3], mask[4], mask[5], mask[6], mask[7], mask[8], mask[9], mask[10], mask[11], mask[12], mask[13], mask[14], mask[15]);
      DEBUGLOG("   -->   MASK: %s", hexdump);

      vector<dmxfilter> &flt = pidmap[make_pair(adapter_index, pid)].filters;
      for (it = flt.begin(); it != flt.end(); ++it)
      {
        if (it->demux_id == demux && it->filter_num == num)
        {
          DEBUGLOG("%s: filter update, demux=%d, filter_num=%d", __FUNCTION__, demux, num);
          break;
        }
      }
      if (it == flt.end())
      {
        DEBUGLOG("%s: inserting new filter, demux=%d, filter_num=%d", __FUNCTION__, demux, num);
        dmxfilter newfilter;
        newfilter.demux_id = demux;
        newfilter.filter_num = num;
        flt.push_back(newfilter);
        it = flt.end() - 1;
      }
      memcpy(it->filter, filter, DMX_FILTER_SIZE);
      memcpy(it->mask, mask, DMX_FILTER_SIZE);
      for (int i = 0; i < DMX_FILTER_SIZE; i++)
        it->filter[i] &= it->mask[i];
    }
    else if (start == 0)
    {
      map<pair<int, int>, pidfilters>::iterator pmit = pidmap.find(make_pair(adapter_index, pid));
      if (pmit != pidmap.end())
      {
        vector<dmxfilter> &flt = pmit->second.filters;
        for (it = flt.begin(); it != flt.end(); ++it)
        {
          if (it->demux_id == demux && it->filter_num == num)
          {
            DEBUGLOG("%s: deleting filter, demux=%d, filter_num=%d", __FUNCTION__, demux, num);
            flt.erase(it);
            if (flt.empty())
            {
              DEBUGLOG("%s: deleted the last filter for pid=%04X, removing list", __FUNCTION__, pid);
              pidmap.erase(pmit);
            }
            break;
          }
        }
      }
    }
  }
  return true;
}

void cDvbapiFilter::StopAllFilters()
{
  cMutexLock lock(&mutex);
  DEBUGLOG("%s", __FUNCTION__);
  pidmap.clear();
}

unsigned char cDvbapiFilter::GetECM(uint8_t adapter_index, ca_descr_t* ca_descr) //XX_ICAM_XX
{
  if (!ca_descr) return 0;
  // the socket thread changes the filters while this runs on a key update
  cMutexLock lock(&mutex);
  map<pair<int, int>, pidfilters>::iterator it;
  for (it = pidmap.begin(); it != pidmap.end(); ++it)
  {
    int adapter_index2 = it->first.first;
    int pid = it->first.second;
    if (adapter_index2 == adapter_index)
    {
      vector<dmxfilter>::iterator it2;
      for (it2 = it->second.filters.begin(); it2 != it->second.filters.end(); ++it2)
      {
        // the CAT, SDT and EMM filters of the demux share its index
        if (pid>0 && ca_descr->index == (uint32_t)it2->demux_id &&
            (it2->filter[0] & 0xFE) == 0x80)
        {
          return ca_descr->parity == 0 ? it2->ecm_even : it2->ecm_odd;
        }
      }
    }
  }
  return 0;
}

void cDvbapiFilter::SetECM(dmxfilter* filter, int adapter_index, int pid, const unsigned char* data, int len)
{
  if (filter && data && len > 4)
  {
    // as OSCam's select_csa_alt() ? get_ecm_mode() : 0; the CAID is unknown
    // until OSCam's ECM_INFO, so the ECM structure has to decide
    int sid = 0;
    uint16_t caid = capmt ? capmt->GetCAIDFromPid(adapter_index, pid, sid) : 0;
    bool videoguard = (caid == 0 || (caid >> 8) == 0x09) && data[4] != 0 && data[2] - data[4] == 4;
    unsigned char ecm = videoguard ? (unsigned char)(data[len - 1] & 0x0F) : 0;
    if (data[0] == 0x80)  //even
    {
      filter->ecm_even = ecm;
    }
    else if (data[0] == 0x81) //odd
    {
      filter->ecm_odd = ecm;
    }
  }
}

void cDvbapiFilter::Deliver(pidfilters &pf, uint8_t adapter_index, int pid, const unsigned char *data, int len)
{
  vector<dmxfilter>::iterator it;
  for (it = pf.filters.begin(); it != pf.filters.end(); ++it)
  {
    // as in the Linux demux: filter[0] is matched against the table id,
    // filter[1..15] against the bytes after the section length
    int i;
    for (i = 0; i < DMX_FILTER_SIZE; i++)
    {
      int pos = i ? i + 2 : 0;
      unsigned char b = pos < len ? data[pos] : 0;
      if ((b & it->mask[i]) != it->filter[i])
        break;
    }
    if (i == DMX_FILTER_SIZE)
    {
      DEBUGLOG("%s: pid=%04X sending section len=%d", __FUNCTION__, pid, len);
      SetECM(&(*it), adapter_index, pid, data, len);
      SockHandler->SendFilterData(it->demux_id, it->filter_num, (unsigned char *) data, len);
    }
  }
}

void cDvbapiFilter::Collect(pidfilters &pf, uint8_t adapter_index, int pid, const unsigned char *data, int len)
{
  if (pf.sectionLen)
    len = min(len, pf.sectionLen - (int) pf.section.size());
  pf.section.insert(pf.section.end(), data, data + len);
  if (!pf.sectionLen && pf.section.size() >= 3)
  {
    // the header was split across packets; anything past the section is stuffing
    pf.sectionLen = 3 + (((pf.section[1] & 0x0F) << 8) | pf.section[2]);
    if ((int) pf.section.size() > pf.sectionLen)
      pf.section.resize(pf.sectionLen);
  }
  if (pf.sectionLen && (int) pf.section.size() == pf.sectionLen)
  {
    Deliver(pf, adapter_index, pid, pf.section.data(), pf.sectionLen);
    pf.section.clear();
    pf.sectionLen = 0;
  }
}

void cDvbapiFilter::Analyze(uint8_t adapter_index, unsigned char *data, int len)
{
  cMutexLock lock(&mutex);
  int pid = ((data[1] << 8) + data[2]) & MAX_CSA_PID;
  if ((unsigned) pid >= MAX_CSA_PID || pid == 0)
    return;
  map<pair<int, int>, pidfilters>::iterator pmit = pidmap.find(make_pair(adapter_index, pid));
  if (pmit == pidmap.end())
    return;
  pidfilters &pf = pmit->second;

  int afc = (data[3] >> 4) & 0x03;                 // adaptation field control
  if (!(afc & 0x01))
    return;                                        // no payload
  int offset = 4;
  if (afc & 0x02)
    offset += 1 + data[4];                         // skip the adaptation field
  if (offset >= TS_SIZE)
    return;
  int cc = data[3] & 0x0F;
  bool continuous = pf.lastcc >= 0 && cc == ((pf.lastcc + 1) & 0x0F);
  pf.lastcc = cc;
  const unsigned char *p = data + offset;
  int n = TS_SIZE - offset;

  if (!(data[1] & 0x40))                           // no section starts here
  {
    if (continuous && !pf.section.empty())
      Collect(pf, adapter_index, pid, p, n);
    else
    {
      pf.section.clear();                          // lost a packet: drop the section
      pf.sectionLen = 0;
    }
    return;
  }

  // PUSI: the pointer field says where the first new section starts; the
  // bytes before it complete the section of the previous packet
  int pointer = *p++;
  n--;
  if (pointer > n)
  {
    pf.section.clear();
    pf.sectionLen = 0;
    return;
  }
  if (continuous && !pf.section.empty())
    Collect(pf, adapter_index, pid, p, pointer);
  pf.section.clear();
  pf.sectionLen = 0;
  p += pointer;
  n -= pointer;

  while (n > 0 && *p != 0xFF)                      // 0xFF: stuffing up to the packet end
  {
    if (n < 3)
    {
      Collect(pf, adapter_index, pid, p, n);       // header continues in the next packet
      break;
    }
    int seclen = 3 + (((p[1] & 0x0F) << 8) | p[2]);
    if (seclen > n)
    {
      Collect(pf, adapter_index, pid, p, n);       // section continues in the next packet
      break;
    }
    Deliver(pf, adapter_index, pid, p, seclen);
    p += seclen;
    n -= seclen;
  }
}
