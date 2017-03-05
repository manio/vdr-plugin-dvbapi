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
#include "cscrypt/des.h"

DeCSA *decsa = NULL;

bool CheckNull(const unsigned char *data, int len)
{
  while (--len >= 0)
    if (data[len])
      return false;
  return true;
}

#ifdef LIBSSL
struct aes_keys_t
{
  AES_KEY even;
  AES_KEY odd;
};

void aes_set_control_words(void *aeskeys, const unsigned char *ev, const unsigned char *od)
{
  AES_set_decrypt_key(ev, 128, &((struct aes_keys_t *) aeskeys)->even);
  AES_set_decrypt_key(od, 128, &((struct aes_keys_t *) aeskeys)->odd);
}

void * aes_get_key_struct(void)
{
  struct aes_keys_t *aeskeys = (struct aes_keys_t *) malloc(sizeof(struct aes_keys_t));
  if (aeskeys)
  {
    static const unsigned char packet[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    aes_set_control_words(aeskeys, packet, packet);
  }
  return aeskeys;
}

void aes_free_key_struct(void *aeskeys)
{
  if (aeskeys)
    free(aeskeys);
}
#endif

DeCSA::DeCSA()
{
#ifndef LIBDVBCSA
  cs = get_suggested_cluster_size();
  DEBUGLOG("clustersize=%d rangesize=%d", cs, cs * 2 + 5);
  range = MALLOC(unsigned char *, (cs * 2 + 5));
  memset(keys, 0, sizeof(keys));
#else
  cs = dvbcsa_bs_batch_size();
  DEBUGLOG("batch_size=%d", cs);
  cs_tsbbatch_even = reinterpret_cast<dvbcsa_bs_batch_s *>(malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s)));
  cs_tsbbatch_odd = reinterpret_cast<dvbcsa_bs_batch_s *>(malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s)));
  memset(cs_key_even, 0, sizeof(cs_key_even));
  memset(cs_key_odd, 0, sizeof(cs_key_odd));
#endif
  memset(cwSeen, 0, sizeof(cwSeen));
  memset(Aes,0,sizeof(Aes));
#ifdef LIBSSL
  memset(csa_aes_keys, 0, sizeof(csa_aes_keys));
#endif
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
#ifdef LIBSSL
  for (int i = 0; i < MAX_CSA_IDX; i++)
    if (csa_aes_keys[i])
      aes_free_key_struct(csa_aes_keys[i]);
#endif
}

void DeCSA::ResetState(void)
{
  DEBUGLOG("%s", __FUNCTION__);
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

#ifdef LIBSSL
bool DeCSA::GetKeyStructAes(int idx)
{
  if (!csa_aes_keys[idx])
    csa_aes_keys[idx] = aes_get_key_struct();
  return csa_aes_keys[idx] != 0;
}
#endif

bool DeCSA::SetDescr(ca_descr_t *ca_descr, bool initial)
{
  DEBUGLOG("%s", __FUNCTION__);
  cMutexLock lock(&mutex);
  int idx = ca_descr->index;
  if (idx < MAX_CSA_IDX && GetKeyStruct(idx))
  {
    DEBUGLOG("%d: %4s key set", idx, ca_descr->parity ? "odd" : "even");
    cwSeen[idx] = time(NULL);
    des_set_key(ca_descr->cw, des_key_schedule[idx][ca_descr->parity]);
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

bool DeCSA::SetDescrAes(ca_descr_aes_t *ca_descr_aes, bool initial)
{
  DEBUGLOG("%s", __FUNCTION__);
#ifdef LIBSSL
  cMutexLock lock(&mutex);
  int idx = ca_descr_aes->index;
  if (idx < MAX_CSA_IDX && GetKeyStructAes(idx))
  {
    DEBUGLOG("%d: %4s aes key set", idx, ca_descr_aes->parity ? "odd" : "even");
    if (ca_descr_aes->parity == 0)
    {
      AES_set_decrypt_key(ca_descr_aes->cw, 128, &((struct aes_keys_t *) csa_aes_keys[idx])->even);
    }
    else
    {
      AES_set_decrypt_key(ca_descr_aes->cw, 128, &((struct aes_keys_t *) csa_aes_keys[idx])->odd);
    }
  }
#endif
  return true;
}

bool DeCSA::SetCaPid(uint8_t adapter_index, ca_pid_t *ca_pid)
{
  cMutexLock lock(&mutex);
  if (ca_pid->index < MAX_CSA_IDX && ca_pid->pid < MAX_CSA_PID)
  {
    pidmap[make_pair(adapter_index, ca_pid->pid)] = ca_pid->index == -1 ? 0 : ca_pid->index;
    DEBUGLOG("%d.%d: set pid 0x%04x", adapter_index, ca_pid->index, ca_pid->pid);
  }
  else
    ERRORLOG("%s: Parameter(s) out of range: adapter_index=%d, pid=0x%04x, index=0x%x", __FUNCTION__, adapter_index, ca_pid->pid, ca_pid->index);
  return true;
}

void DeCSA::SetAlgo(uint32_t index, uint32_t usedAlgo)
{
  if (index < MAX_CSA_IDX)
    algo[index] = usedAlgo;
}

void DeCSA::SetAes(uint32_t index, bool usedAes)
{
  if (index < MAX_CSA_IDX)
    Aes[index] = usedAes;
}

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

  int offset;
  int currIdx = -1;
#ifndef LIBDVBCSA
  int r = -2, ccs = 0;
  bool newRange = true;
  range[0] = 0;
#else
  int ccs = 0;
  int payload_len;
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
    /*
       we could have the following values:
       '00' = Not scrambled
       '01' (0x40) = Reserved for future use
       '10' (0x80) = Scrambled with even key
       '11' (0xC0) = Scrambled with odd key
    */
    if (ev_od & 0x80)
    {                           // encrypted
      offset = ts_packet_get_payload_offset(data + l);
#ifdef LIBDVBCSA
      payload_len = TS_SIZE - offset;
#endif
      int pid = ((data[l + 1] << 8) + data[l + 2]) & MAX_CSA_PID;
      int idx = pidmap[make_pair(adapter_index, pid)];
      if ((pid < MAX_CSA_PID) && (currIdx < 0 || idx == currIdx))
      {                         // same or no index
        currIdx = idx;
        // return if the key is expired
        if (!Aes[currIdx] && CheckExpiredCW && time(NULL) - cwSeen[currIdx] > MAX_KEY_WAIT)
          return false;
        if (algo[currIdx] == CA_ALGO_DES)
        {
          if ((ev_od & 0x40) == 0)
          {
            for (int j = offset; j + 7 < 188; j += 8)
              des(&data[l + j], des_key_schedule[currIdx][0], 0);

          }
          else
          {
            for (int j = offset; j + 7 < 188; j += 8)
              des(&data[l + j], des_key_schedule[currIdx][1], 0);

          }
          data[l + 3] &= 0x3f;    // consider it decrypted now
        }
#ifdef LIBSSL
        else if (Aes[currIdx])
        {
          AES_KEY aes_key;
          data[l + 3] &= 0x3f;    // consider it decrypted now

          if (data[l+3] & 0x20)
          {
            if ((188 - offset) >> 4 == 0)
              return true;
          }
          if (((ev_od & 0x40) >> 6) == 0)
          {
            aes_key = ((struct aes_keys_t *) csa_aes_keys[currIdx])->even;
          }
          else
          {
            aes_key = ((struct aes_keys_t *) csa_aes_keys[currIdx])->odd;
          }
          for (int j = offset; j + 16 <= 188; j += 16)
            AES_ecb_encrypt(&data[l + j], &data[l + j], &aes_key, AES_DECRYPT);
        }
#endif
        else
        {
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
  if (algo[currIdx] == CA_ALGO_DES || Aes[currIdx])
    return true;
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
