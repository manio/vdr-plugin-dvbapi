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

#ifndef ___FILTER_H
#define ___FILTER_H

#include <map>
#include <vector>
#include "DeCSA.h"
#include <linux/dvb/dmx.h>

using namespace std;

struct dmxfilter
{
  int demux_id;
  int filter_num;
  unsigned char filter[DMX_FILTER_SIZE];
  unsigned char mask[DMX_FILTER_SIZE];

  unsigned char *data;    //if ecm is spanned over TS packet, hold the part here
  unsigned int len;       //total len
  unsigned int size;      //size of saved data
  int lastcc;             //last continuity counter

  unsigned char ecm_even;
  unsigned char ecm_odd;
};

class cDvbapiFilter
{
private:
  cMutex mutex;
  map<pair<int, int>, vector<dmxfilter>*> pidmap;

public:
  ~cDvbapiFilter();
  void Analyze(uint8_t adapter_index, unsigned char *data, int len);
  bool SetFilter(uint8_t adapter_index, int pid, int start, unsigned char demux, unsigned char num, unsigned char *filter, unsigned char *mask);
  void StopAllFilters();
  void SetECM(dmxfilter* filter, int adapter_index, int pid, const unsigned char* data, int len);
  unsigned char GetECM(uint8_t adapter_index, ca_descr_t* ca_descr);
};

extern cDvbapiFilter *filter;

#endif // ___FILTER_H
