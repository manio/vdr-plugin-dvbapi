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

cDvbapiFilter::cDvbapiFilter()
{
  memset(pidmap, 0, sizeof(pidmap));
}

cDvbapiFilter::~cDvbapiFilter()
{
  StopAllFilters();
}

bool cDvbapiFilter::SetFilter(uint8_t adapter_index, int pid, int start, unsigned char demux, unsigned char num, unsigned char *filter, unsigned char *mask)
{
  cMutexLock lock(&mutex);
  if ((unsigned) pid <= MAX_CSA_PIDS &&
      adapter_index >= 0 && adapter_index < MAX_ADAPTERS)
  {
    DEBUGLOG("%s: adapter=%d set FILTER pid=%04X start=%d, demux=%d, filter=%d", __FUNCTION__, adapter_index, pid, start, demux, num);

    vector<dmxfilter> *flt = pidmap[adapter_index][pid];
    vector<dmxfilter>::iterator it;
    if (start == 1 && filter && mask)
    {
      char hexdump[16 * 3];
      sprintf(hexdump, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", filter[0], filter[1], filter[2], filter[3], filter[4], filter[5], filter[6], filter[7], filter[8], filter[9], filter[10], filter[11], filter[12], filter[13], filter[14], filter[15]);
      DEBUGLOG("   --> FILTER: %s", hexdump);
      sprintf(hexdump, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", mask[0], mask[1], mask[2], mask[3], mask[4], mask[5], mask[6], mask[7], mask[8], mask[9], mask[10], mask[11], mask[12], mask[13], mask[14], mask[15]);
      DEBUGLOG("   -->   MASK: %s", hexdump);

      int updated = 0;
      if (flt)                  //we have filters assigned with this pid
      {
        for (it = flt->begin(); it != flt->end(); ++it)
        {
          if (it->demux_id == demux && it->filter_num == num)
          {
            DEBUGLOG("%s: filter update, demux=%d, filter_num=%d", __FUNCTION__, demux, num);
            memcpy(it->filter, filter, 16);
            memcpy(it->mask, mask, 16);
            updated = 1;
            break;
          }
        }
      }
      else                      //it seems this is the first filter for this pid - create a list
      {
        flt = new vector<dmxfilter>;
        pidmap[adapter_index][pid] = flt;
      }

      if (!updated)
      {
        DEBUGLOG("%s: inserting new filter, demux=%d, filter_num=%d", __FUNCTION__, demux, num);
        dmxfilter newfilter;
        newfilter.demux_id = demux;
        newfilter.filter_num = num;
        newfilter.data = NULL;
        memcpy(newfilter.filter, filter, 16);
        memcpy(newfilter.mask, mask, 16);
        flt->push_back(newfilter);
      }
    }
    else if (flt && start == 0)
    {
      for (it = flt->begin(); it != flt->end(); ++it)
      {
        if (it->demux_id == demux && it->filter_num == num)
        {
          DEBUGLOG("%s: deleting filter, demux=%d, filter_num=%d", __FUNCTION__, demux, num);
          if (it->data)
            delete it->data;
          flt->erase(it);

          if (flt->empty())
          {
            DEBUGLOG("%s: deleted the last filter for pid=%04X, removing list", __FUNCTION__, pid);
            delete flt;
            pidmap[adapter_index][pid] = NULL;
          }

          break;
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

  // firstly releasing data
  for (int i = 0; i < MAX_ADAPTERS; i++)
  {
    for (int j = 0; j < MAX_CSA_PIDS; j++)
    {
      vector<dmxfilter> *flt = pidmap[i][j];
      if (flt)                  //filter list assigned to this pid
      {
        vector<dmxfilter>::iterator it;
        for (it = flt->begin(); it != flt->end(); ++it)
        {
          if (it->data)
            delete it->data;
        }
        flt->clear();
        delete flt;
      }
    }
  }
  // clearing pointers
  memset(pidmap, 0, sizeof(pidmap));
}

void cDvbapiFilter::Analyze(uint8_t adapter_index, unsigned char *data, int len)
{
  cMutexLock lock(&mutex);
  int pid = ((data[1] << 8) + data[2]) & 0x1FFF;
  if ((unsigned) pid <= MAX_CSA_PIDS && pid > 0)
  {
    vector<dmxfilter> *flt = pidmap[adapter_index][pid];
    if (flt)                    //filter list assigned to this pid
    {
      vector<dmxfilter>::iterator it;
      for (it = flt->begin(); it != flt->end(); ++it)
      {
        //PUSI flag set, the first TS packet
        if (data[1] & 0x40)
        {
          //checking filter match
          unsigned char *filter = it->filter;
          unsigned char *mask = it->mask;
          unsigned char *dat = data + 4 + 1;
          int i = 0, max = 16 - 1;
          if ((dat[i] & mask[i]) != filter[i])
            continue;
          else
            dat = data + 4 + 3;
          for (i = 1; i < max; i++)
          {
            if ((dat[i] & mask[i]) != filter[i])
              break;
          }
          if (i == max)
          {
            int tablelen = ((data[4 + 2] << 8) + data[4 + 3]) & 0x0FFF;
            if (tablelen <= TS_SIZE - 8)        //data fits in one TS_PACKET
            {
              DEBUGLOG("%s: all data in one TS packet, immediate send", __FUNCTION__);
              SockHandler->SendFilterData(it->demux_id, it->filter_num, data + 4 + 1, tablelen + 3);
            }
            else                //copying the first part of data
            {
              DEBUGLOG("%s: saving first part of data", __FUNCTION__);
              if (it->data)
                delete it->data;
              it->data = new unsigned char[tablelen + 3];
              it->len = tablelen;

              memcpy(it->data, data + 5, TS_SIZE - 5);  //copy data
              it->size = TS_SIZE - 5 - 3;
              it->lastcc = data[3] & 0x0f;      //TS header -> continuity counter
            }
            return;
          }
        }
        else if (it->data && (it->lastcc + 1 == (data[3] & 0x0f)))      //we have a part of data for this filter
        {
          int to_copy = it->len - it->size;
          if (to_copy > TS_SIZE - 4)
            to_copy = TS_SIZE - 4;
          DEBUGLOG("%s: PUSI=0, to_copy=%d", __FUNCTION__, to_copy);

          memcpy(it->data + 3 + it->size, data + 4, to_copy);   //copy data
          it->size += to_copy;
          it->lastcc = data[3] & 0x0f;  //TS header -> continuity counter

          if (it->size == it->len)      //we've got all data
          {
            DEBUGLOG("%s: filter data assembled, sending len=%d", __FUNCTION__, it->len + 3);
            SockHandler->SendFilterData(it->demux_id, it->filter_num, it->data, it->len + 3);
            //we don't need this data anymore
            delete it->data;
            it->data = NULL;
          }
          return;
        }
      }
    }
  }
}
