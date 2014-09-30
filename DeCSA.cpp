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

#include "DeCSA.h"
#include "Log.h"

#ifndef LIBDVBCSA
#include "FFdecsa/FFdecsa.h"
#endif

DeCSA *decsa = NULL;

bool CheckNull(const unsigned char *data, int len)
{
  while (--len >= 0)
    if (data[len])
      return false;
  return true;
}

DeCSA::DeCSA(int CardIndex)
{
  cardindex = CardIndex;
#ifndef LIBDVBCSA
  cs = get_suggested_cluster_size();
  DEBUGLOG("%d: clustersize=%d rangesize=%d", cardindex, cs, cs * 2 + 5);
  range = MALLOC(unsigned char *, (cs * 2 + 5));
  memset(keys, 0, sizeof(keys));
#else
  cs = dvbcsa_bs_batch_size();
  DEBUGLOG("%d: batch_size=%d", cardindex, cs);
  cs_tsbbatch_even = reinterpret_cast<dvbcsa_bs_batch_s *>(malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s)));
  cs_tsbbatch_odd = reinterpret_cast<dvbcsa_bs_batch_s *>(malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s)));
  memset(cs_key_even, 0, sizeof(cs_key_even));
  memset(cs_key_odd, 0, sizeof(cs_key_odd));
#endif
  memset(cwSeen, 0, sizeof(cwSeen));
  memset(pidmap, 0, sizeof(pidmap));
  ResetState();
}

DeCSA::~DeCSA()
{
  for (int i = 0; i < MAX_CSA_IDX; i++)
#ifndef LIBDVBCSA
    if (keys[i])
      free_key_struct(keys[i]);
  free(range);
#else
  {
    if (cs_key_even[i])
      dvbcsa_bs_key_free(cs_key_even[i]);
    if (cs_key_odd[i])
      dvbcsa_bs_key_free(cs_key_odd[i]);
  }
  free(cs_tsbbatch_even);
  free(cs_tsbbatch_odd);
#endif
}

void DeCSA::ResetState(void)
{
  DEBUGLOG("%d: reset state", cardindex);
#ifndef LIBDVBCSA
  lastData = 0;
#endif
}

bool DeCSA::GetKeyStruct(int idx)
{
#ifndef LIBDVBCSA
  if (!keys[idx])
    keys[idx] = get_key_struct();
  return keys[idx] != 0;
#else
  if (!cs_key_even[idx])
    cs_key_even[idx] = dvbcsa_bs_key_alloc();
  if (!cs_key_odd[idx])
    cs_key_odd[idx] = dvbcsa_bs_key_alloc();
  return (cs_key_even[idx] != 0) && (cs_key_odd[idx] != 0);
#endif
}

bool DeCSA::SetDescr(ca_descr_t *ca_descr, bool initial)
{
  DEBUGLOG("%s", __FUNCTION__);
  cMutexLock lock(&mutex);
  int idx = ca_descr->index;
  if (idx < MAX_CSA_IDX && GetKeyStruct(idx))
  {
    DEBUGLOG("%d.%d: %4s key set", cardindex, idx, ca_descr->parity ? "odd" : "even");
    cwSeen[idx] = time(NULL);
    if (ca_descr->parity == 0)
    {
#ifndef LIBDVBCSA
      set_even_control_word(keys[idx], ca_descr->cw);
#else
      dvbcsa_bs_key_set(ca_descr->cw, cs_key_even[idx]);
#endif
    }
    else
    {
#ifndef LIBDVBCSA
      set_odd_control_word(keys[idx], ca_descr->cw);
#else
      dvbcsa_bs_key_set(ca_descr->cw, cs_key_odd[idx]);
#endif
    }
  }
  return true;
}

bool DeCSA::SetCaPid(uint8_t adapter_index, ca_pid_t *ca_pid)
{
  cMutexLock lock(&mutex);
  if (ca_pid->index < MAX_CSA_IDX && ca_pid->pid < MAX_CSA_PIDS &&
      adapter_index >= 0 && adapter_index < MAX_ADAPTERS)
  {
    pidmap[adapter_index][ca_pid->pid] = ca_pid->index;
    DEBUGLOG("%d.%d: set pid %04x", cardindex, ca_pid->index, ca_pid->pid);
  }
  return true;
}

#ifdef LIBDVBCSA
unsigned char ts_packet_get_payload_offset(unsigned char *ts_packet)
{
  if (ts_packet[0] != TS_SYNC_BYTE)
    return 0;

  unsigned char adapt_field   = (ts_packet[3] &~ 0xDF) >> 5; // 11x11111
  unsigned char payload_field = (ts_packet[3] &~ 0xEF) >> 4; // 111x1111

  if (!adapt_field && !payload_field)     // Not allowed
    return 0;

  if (adapt_field)
  {
    unsigned char adapt_len = ts_packet[4];
    if (payload_field && adapt_len > 182) // Validity checks
      return 0;
    if (!payload_field && adapt_len > 183)
      return 0;
    if (adapt_len + 4 > TS_SIZE)  // adaptation field takes the whole packet
      return 0;
    return 4 + 1 + adapt_len;     // ts header + adapt_field_len_byte + adapt_field_len
  }
  else
  {
    return 4; // No adaptation, data starts directly after TS header
  }
}
#endif

bool DeCSA::Decrypt(uint8_t adapter_index, unsigned char *data, int len, bool force)
{
  cMutexLock lock(&mutex);
#ifndef LIBDVBCSA
  if (!range)
#else
  if (!cs_tsbbatch_even || !cs_tsbbatch_odd)
#endif
  {
    ERRORLOG("%s: Error allocating memory for DeCSA", __FUNCTION__);
    return false;
  }

#ifndef LIBDVBCSA
  int r = -2, ccs = 0, currIdx = -1;
  bool newRange = true;
  range[0] = 0;
#else
  int ccs = 0, currIdx = -1;
  int payload_len, offset;
  int cs_fill_even = 0;
  int cs_fill_odd = 0;
#endif
  len -= (TS_SIZE - 1);
  int l;
  for (l = 0; l < len; l += TS_SIZE)
  {
    if (data[l] != TS_SYNC_BYTE)
    {                           // let higher level cope with that
      break;
    }
    unsigned int ev_od = data[l + 3] & 0xC0;
    if (ev_od == 0x80 || ev_od == 0xC0)
    {                           // encrypted
#ifdef LIBDVBCSA
      offset = ts_packet_get_payload_offset(data + l);
      payload_len = TS_SIZE - offset;
#endif
      int idx = pidmap[adapter_index][((data[l + 1] << 8) + data[l + 2]) & (MAX_CSA_PIDS - 1)];
      if (currIdx < 0 || idx == currIdx)
      {                         // same or no index
        currIdx = idx;
        // return if the key is expired
        if (CheckExpiredCW && time(NULL) - cwSeen[currIdx] > MAX_KEY_WAIT)
          return false;
#ifndef LIBDVBCSA
        if (newRange)
        {
          r += 2;
          newRange = false;
          range[r] = &data[l];
          range[r + 2] = 0;
        }
        range[r + 1] = &data[l + TS_SIZE];
#else
        data[l + 3] &= 0x3f;    // consider it decrypted now
        if (((ev_od & 0x40) >> 6) == 0)
        {
          cs_tsbbatch_even[cs_fill_even].data = &data[l + offset];
          cs_tsbbatch_even[cs_fill_even].len = payload_len;
          cs_fill_even++;
        }
        else
        {
          cs_tsbbatch_odd[cs_fill_odd].data = &data[l + offset];
          cs_tsbbatch_odd[cs_fill_odd].len = payload_len;
          cs_fill_odd++;
        }
#endif
        if (++ccs >= cs)
          break;
      }
#ifndef LIBDVBCSA
      else
        newRange = true;        // other index, create hole
#endif
    }
    else
    {                           // unencrypted
      // nothing, we don't create holes for unencrypted packets
    }
  }
#ifndef LIBDVBCSA
  if (r >= 0)
  {                             // we have some range
    if (ccs >= cs || force)
    {
      if (GetKeyStruct(currIdx))
      {
        int n = decrypt_packets(keys[currIdx], range);
        if (n > 0)
          return true;
      }
    }
  }
#else
  if (GetKeyStruct(currIdx))
  {
    if (cs_fill_even)
    {
      cs_tsbbatch_even[cs_fill_even].data = NULL;
      dvbcsa_bs_decrypt(cs_key_even[currIdx], cs_tsbbatch_even, 184);
    }
    if (cs_fill_odd)
    {
      cs_tsbbatch_odd[cs_fill_odd].data = NULL;
      dvbcsa_bs_decrypt(cs_key_odd[currIdx], cs_tsbbatch_odd, 184);
    }
    return true;
  }
#endif
  return false;
}
