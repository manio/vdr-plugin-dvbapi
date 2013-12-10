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

bool CheckNull(const unsigned char *data, int len)
{
  while (--len >= 0)
    if (data[len])
      return false;
  return true;
}

DeCSA::DeCSA(int CardIndex)
 : stall(MAX_STALL_MS)
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
  cs_tsbbatch_even = (dvbcsa_bs_batch_s *) malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s));
  cs_tsbbatch_odd = (dvbcsa_bs_batch_s *) malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s));
  memset(cs_key_even, 0, sizeof(cs_key_even));
  memset(cs_key_odd, 0, sizeof(cs_key_odd));
#endif
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
  memset(even_odd, 0, sizeof(even_odd));
  memset(flags, 0, sizeof(flags));
#ifndef LIBDVBCSA
  lastData = 0;
#endif
}

void DeCSA::SetActive(bool on)
{
//  if (!on && active)
//    ResetState();
  active = true;
  DEBUGLOG("%d: set active %s", cardindex, active ? "on" : "off");
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
    if (!initial && active && ca_descr->parity == (even_odd[idx] & 0x40) >> 6)
    {
      if (flags[idx] & (ca_descr->parity ? FL_ODD_GOOD : FL_EVEN_GOOD))
      {
        DEBUGLOG("%d.%d: %s key in use (%d ms)", cardindex, idx, ca_descr->parity ? "odd" : "even", MAX_REL_WAIT);
        if (wait.TimedWait(mutex, MAX_REL_WAIT))
          DEBUGLOG("%d.%d: successfully waited for release", cardindex, idx);
        else
          ERRORLOG("%d.%d: timed out. setting anyways", cardindex, idx);
      }
      else
        DEBUGLOG("%d.%d: late key set...", cardindex, idx);
    }
    DEBUGLOG("%d.%d: %4s key set", cardindex, idx, ca_descr->parity ? "odd" : "even");
    if (ca_descr->parity == 0)
    {
#ifndef LIBDVBCSA
      set_even_control_word(keys[idx], ca_descr->cw);
#else
      dvbcsa_bs_key_set(ca_descr->cw, cs_key_even[idx]);
#endif
      if (!CheckNull(ca_descr->cw, 8))
        flags[idx] |= FL_EVEN_GOOD | FL_ACTIVITY;
      else
        DEBUGLOG("%d.%d: zero even CW", cardindex, idx);
      wait.Broadcast();
    }
    else
    {
#ifndef LIBDVBCSA
      set_odd_control_word(keys[idx], ca_descr->cw);
#else
      dvbcsa_bs_key_set(ca_descr->cw, cs_key_odd[idx]);
#endif
      if (!CheckNull(ca_descr->cw, 8))
        flags[idx] |= FL_ODD_GOOD | FL_ACTIVITY;
      else
        DEBUGLOG("%d.%d: zero odd CW", cardindex, idx);
      wait.Broadcast();
    }
  }
  return true;
}

bool DeCSA::SetCaPid(ca_pid_t *ca_pid)
{
  cMutexLock lock(&mutex);
  if (ca_pid->index < MAX_CSA_IDX && ca_pid->pid < MAX_CSA_PIDS)
  {
    pidmap[ca_pid->pid] = ca_pid->index;
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

bool DeCSA::Decrypt(unsigned char *data, int len, bool force)
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
#ifndef LIBDVBCSA
      if (ccs)
        force = true;           // prevent buffer stall
#endif
      break;
    }
    unsigned int ev_od = data[l + 3] & 0xC0;
    if (ev_od == 0x80 || ev_od == 0xC0)
    {                           // encrypted
#ifdef LIBDVBCSA
      offset = ts_packet_get_payload_offset(data + l);
      payload_len = TS_SIZE - offset;
#endif
      int idx = pidmap[((data[l + 1] << 8) + data[l + 2]) & (MAX_CSA_PIDS - 1)];
      if (currIdx < 0 || idx == currIdx)
      {                         // same or no index
#ifdef LIBDVBCSA
        data[l + 3] &= 0x3f;    // consider it decrypted now
#endif
        currIdx = idx;
        if (ccs == 0 && ev_od != even_odd[idx])
        {
          even_odd[idx] = ev_od;
          wait.Broadcast();
          bool doWait = false;
          if (ev_od & 0x40)
          {
            flags[idx] &= ~FL_EVEN_GOOD;
            if (!(flags[idx] & FL_ODD_GOOD))
              doWait = true;
          }
          else
          {
            flags[idx] &= ~FL_ODD_GOOD;
            if (!(flags[idx] & FL_EVEN_GOOD))
              doWait = true;
          }
          /*if (doWait)
          {
            if (flags[idx] & FL_ACTIVITY)
            {
              flags[idx] &= ~FL_ACTIVITY;
              if (wait.TimedWait(mutex, MAX_KEY_WAIT))
                DEBUGLOG("%d.%d: successfully waited for key", cardindex, idx);
              else
                DEBUGLOG("%d.%d: timed out. proceeding anyways", cardindex, idx);
            }
          }*/
        }
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
  int scanTS = l / TS_SIZE;
  int stallP = ccs * 100 / scanTS;

  if (r >= 0 && ccs < cs && !force)
  {
    if (lastData == data && stall.TimedOut())
    {
      force = true;
    }
    else if (stallP <= 10 && scanTS >= cs)
    {
      force = true;
    }
  }
  lastData = data;

  if (r >= 0)
  {                             // we have some range
    if (ccs >= cs || force)
    {
      if (GetKeyStruct(currIdx))
      {
        int n = decrypt_packets(keys[currIdx], range);
        if (n > 0)
        {
          stall.Set(MAX_STALL_MS);
          return true;
        }
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
      cs_fill_even = 0;
    }
    if (cs_fill_odd)
    {
      cs_tsbbatch_odd[cs_fill_odd].data = NULL;
      dvbcsa_bs_decrypt(cs_key_odd[currIdx], cs_tsbbatch_odd, 184);
      cs_fill_odd = 0;
    }

    stall.Set(MAX_STALL_MS);
    return true;
  }
#endif
  return false;
}
