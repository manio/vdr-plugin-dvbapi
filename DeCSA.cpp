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

/* ----------------- FASTECM description:
   The encrypted control word is broadcast in an ECM approximately once every two seconds
   The Control Word used to encrypt the transport stream packets are changed regularly,
   usually every 10 seconds.If the Control Words change stops for whatever reason the STBs can use the same Control Word
   to decrypt the incoming signal until the problem is fixed.This is a serious security issue.

   In each PID header there are 2 bits telling the decoder if the Odd or Even Control Word should be used.The ECM
   normally contains two Control Words.This mechanism allows the ECM to carry both the Control Word currently used
   and the Control Word which will be used for scrambling the next time the Control Word changes.This ensures that the
   STB always has the Control Word needed to descramble the content.

   Sky Germany only has one Control Word.
   We can see the CW around 620ms before it shoul be used.
*/

#include "DeCSA.h"
#include "Log.h"
#include "cscrypt/des.h"

DeCSA *decsa = NULL;

#define lldcast long long int
bool IsFastECMCAID(int caCaid)
{
  if (caCaid == 0x09C4 || caCaid == 0x098C || caCaid == 0x098D || caCaid == 0x09AF || //SKY DE 09AF is OBSOLETE
      caCaid == 0x09CD || //Sky IT
      caCaid == 0x0963)   //Sky UK
  {
    return true;
  }

  return false;
}

class cMutexLockHelper {
private:
  cMutexLock *pmutexLock;
  cMutex *pmutex;
public:
  cMutexLockHelper(cMutex *Mutex = NULL, bool block = true)
  {
    pmutexLock = NULL;
    pmutex = Mutex;
    if (block)
      pmutexLock = new cMutexLock(pmutex);
  };
  ~cMutexLockHelper()
  {
    if (pmutexLock) delete pmutexLock;
    pmutexLock = NULL;
    pmutex = NULL;
  }

  void UnLock()
  {
    if (pmutexLock != NULL) delete pmutexLock;
    pmutexLock = NULL;
  }

  void ReLock()
  {
    if (pmutexLock == NULL)
    pmutexLock = new cMutexLock(pmutex);
  }
};

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

DeCSAKey::DeCSAKey()
{
  Aes = false;
  index = -1;

#ifndef LIBDVBCSA
  key = NULL;
#else
  cs_key_even = NULL;
  cs_key_odd = NULL;
#endif

#ifdef LIBSSL
  csa_aes_key = NULL;
#endif
  lastcwlog = 0;

  cwSeen = 0;
}

DeCSAKey::~DeCSAKey()
{
#ifndef LIBDVBCSA
  if (key)
  {
    cMutexLock lock(&mutexKEY);
    free_key_struct(key);
  }
  key = NULL;
#else
{
  cMutexLock lock(&mutexKEY);
  if (cs_key_even)
    dvbcsa_bs_key_free(cs_key_even);
  cs_key_even = NULL;
  if (cs_key_odd)
    dvbcsa_bs_key_free(cs_key_odd);
  cs_key_odd = NULL;
}
#endif

#ifdef LIBSSL
  if (csa_aes_key)
  {
    cMutexLock lock(&mutexKEY);
    aes_free_key_struct(csa_aes_key);
  }
  csa_aes_key = NULL;
#endif
}

void DeCSAKey::SetAes(uint32_t usedAes)
{
  cMutexLock lock(&mutexKEY);
  Aes = usedAes;
}

uint32_t DeCSAKey::GetAes()
{
  cMutexLock lock(&mutexKEY);
  return Aes;
}

void DeCSAKey::SetAlgo(uint32_t usedAlgo)
{
  cMutexLock lock(&mutexKEY);
  algo = usedAlgo;
}

uint32_t DeCSAKey::GetAlgo()
{
  cMutexLock lock(&mutexKEY);
  return algo;
}

#ifdef LIBSSL
bool DeCSAKey::GetorCreateAesKeyStruct()
{
  cMutexLock lock(&mutexKEY);
  if (!csa_aes_key)
  {
    DEBUGLOG("GetorCreateAesKeyStruct - keyindex:%d", index);
    csa_aes_key = aes_get_key_struct();
  }
  return csa_aes_key != 0;
}
#endif

bool DeCSAKey::GetorCreateKeyStruct()
{
  cMutexLock lock(&mutexKEY);
#ifndef LIBDVBCSA
  if (!key)
  {
    DEBUGLOG("GetorCreateKeyStruct - keyindex:%d", index);
    key = get_key_struct();
  }
  return key != 0;
#else
  if (!cs_key_even)
  {
    DEBUGLOG("GetorCreateKeyStruct - even, keyindex:%d", index);
    cs_key_even = dvbcsa_bs_key_alloc();
  }
  if (!cs_key_odd)
  {
    DEBUGLOG("GetorCreateKeyStruct - odd, keyindex:%d", index);
    cs_key_odd = dvbcsa_bs_key_alloc();
  }
  return (cs_key_even != 0) && (cs_key_odd != 0);
#endif
}

bool DeCSAKey::CWExpired()
{
  cMutexLock lock(&mutexKEY);
  if (CheckExpiredCW)
  {
    time_t tnow = time(NULL);
    if (CheckExpiredCW && cwSeen > 0 && (tnow - cwSeen) > MAX_KEY_WAIT)
    {
      if ((tnow - lastcwlog) > 10)
      {
        lastcwlog = tnow;
        DEBUGLOG("%s: CheckExpiredCW key is expired", __FUNCTION__);
      }
      return true;
    }
    else
    {
      lastcwlog = tnow;
    }
  }
  return false;
}

#ifndef LIBDVBCSA
bool DeCSAKey::SetFastECMCaidSid(int caid, int sid)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    setFastECMCaidSid(key, caid,sid);
    return true;
  }
  return false;
}

int DeCSAKey::Set_FastECM_CW_Parity(int pid, int parity, bool bforce, int& oldparity, bool& bfirsttimecheck, bool& bnextparityset, bool& bactivparitypatched)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    return set_FastECM_CW_Parity(key, pid, parity, bforce, oldparity, bfirsttimecheck, bnextparityset, bactivparitypatched);
  }
  return 1;
}

void DeCSAKey::SetFastECMPid(int pid)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    setFastECMPid(key, pid);
  }
}

void DeCSAKey::Get_FastECM_CAID(int* caid)
{
  cMutexLock lock(&mutexKEY);
  *caid = 0;
  if (key)
    get_FastECM_CAID(key,caid);
}

void DeCSAKey::Get_FastECM_SID(int* caSid)
{
  cMutexLock lock(&mutexKEY);
  *caSid = 0;
  if (key)
  {
    get_FastECM_SID(key, caSid);
  }
}

void DeCSAKey::Get_FastECM_PID(int* caPid)
{
  cMutexLock lock(&mutexKEY);
  *caPid = 0;
  if (key)
  {
    get_FastECM_PID(key, caPid);
  }
}

bool DeCSAKey::Get_FastECM_struct(FAST_ECM& fecm)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    FAST_ECM* fe=get_FastECM_struct(key);
    fecm = *fe;
    return true;
  }
  return false;
}

bool DeCSAKey::GetActiveParity(int pid, int& aparity, int& aparity2)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    getActiveParity(key,pid, aparity, aparity2);
    return true;
  }
  return false;
}

void DeCSAKey::InitFastEcmOnCaid(int Caid)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    struct FAST_ECM* sf = get_FastECM_struct(key);
    if (sf && Caid == sf->csaCaid)
    {
      sf->oddparityTime = 0;
      sf->evenparityTime = 0;
      sf->nextparity = 0;

      sf->activparity.clear();
      sf->activparity2.clear();
    }
  }
}

void DeCSAKey::SetActiveParity2(int pid,int parity2)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    FAST_ECM* fe = get_FastECM_struct(key);
    fe->activparity2[pid] = parity2;
  }
}

int DeCSAKey::Decrypt_packets(unsigned char **cluster)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    return decrypt_packets(key, cluster);
  }
  else
  {
    DEBUGLOG("%s: ind:%d Decrypt_packets key is null", __FUNCTION__, index);
  }
  return 0;
}

bool DeCSAKey::Get_control_words(unsigned char *even, unsigned char *odd)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    get_control_words(key, even, odd);
    return true;
  }
  return false;
}
#endif

void DeCSAKey::Des(uint8_t* data, unsigned char parity)
{
  cMutexLock lock(&mutexKEY);
  des(data, des_key_schedule[parity], 0);
}

void DeCSAKey::Des_set_key(const unsigned char *cw, unsigned char parity)
{
  cMutexLock lock(&mutexKEY);
  cwSeen = time(NULL);
  des_set_key(cw, des_key_schedule[parity]);
}

bool DeCSAKey::Set_even_control_word(const unsigned char *even, const unsigned char ecm)
{
  cMutexLock lock(&mutexKEY);
#ifndef LIBDVBCSA
  if (key)
  {
    set_even_control_word(key, even, ecm);
    return true;
  }
#else
  if (cs_key_even)
  {
#ifdef LIBDVBCSA_NEW
    dvbcsa_bs_key_set(even, cs_key_even,ecm); //todo lib must be upgraded to support this.
#else
    dvbcsa_bs_key_set(even, cs_key_even); //todo lib must be upgraded to support this.
#endif
    return true;
  }
#endif
  return false;
}

bool DeCSAKey::Set_odd_control_word(const unsigned char *odd, const unsigned char ecm)
{
  cMutexLock lock(&mutexKEY);
#ifndef LIBDVBCSA
  if (key)
  {
    set_odd_control_word(key, odd, ecm);
    return true;
  }
#else
  if (cs_key_odd)
  {
#ifdef LIBDVBCSA_NEW
    dvbcsa_bs_key_set(odd, cs_key_odd, ecm); //todo lib must be upgraded to support this.
#else
    dvbcsa_bs_key_set(odd, cs_key_odd); //todo lib must be upgraded to support this.
#endif
    return true;
  }
#endif
  return false;
}

#ifndef LIBDVBCSA
void DeCSAKey::Init_Parity2(bool binitcsa)
{
  cMutexLock lock(&mutexKEY);
  if (key)
  {
    if (binitcsa)
      DEBUGLOG("Init_Parity keyindex:%d", index);
    else
      DEBUGLOG("Init_Parity from Timeout keyindex:%d", index);

    Init_FastECM(key, binitcsa);
  }
}
#endif

DeCSAAdapter::DeCSAAdapter()
{
  cardindex = -1;

  bCW_Waiting = false;
  bAbort = false;

#ifndef LIBDVBCSA
  csnew = get_suggested_cluster_size();
  rangenew = MALLOC(unsigned char *, (csnew * 2 + 5));
#else
  cs = dvbcsa_bs_batch_size();
  cs_tsbbatch_even = reinterpret_cast<dvbcsa_bs_batch_s *>(malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s)));
  cs_tsbbatch_odd = reinterpret_cast<dvbcsa_bs_batch_s *>(malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s)));
#endif
}

DeCSAAdapter::~DeCSAAdapter()
{
  cMutexLockHelper lockDecrypt(&mutexDecrypt);

#ifndef LIBDVBCSA
  free(rangenew);
#else
  free(cs_tsbbatch_even);
  free(cs_tsbbatch_odd);
#endif
}

void DeCSAAdapter::CancelWait()
{
  if (bCW_Waiting)
  {
    DEBUGLOG("%s: decsa CW Waiting", __FUNCTION__);
    bAbort = true;
    cMutexLock lock(&mutexStopDecrypt);
    bAbort = false;
    DEBUGLOG("%s: decsa CW Waiting Aborted", __FUNCTION__);
  }
}

#ifndef LIBDVBCSA
void DeCSAAdapter::Init_Parity(DeCSAKey *keys, int sid, int slot,bool bdelete)
{
  cMutexLock lock(&mutexAdapter);

  if (sid < 0 && slot < 0) return;
  if (sid >= 0)
    DEBUGLOG("Init_Parity cardindex:%d SID %d (0x%04X)", cardindex, sid, sid);
  else
    DEBUGLOG("Init_Parity cardindex:%d Slot %d", cardindex, slot);

  bool bEND = false;
  do
  {
    bEND = true;
    map<int, unsigned char>::iterator it;
    for (it = AdapterPidMap.begin(); it != AdapterPidMap.end(); ++it)
    {
      int ipid = it->first;
      int iidx = it->second;

      int caCaid = -1;
      int caSid = -1;
      int caPid = -1;

      keys[iidx].Get_FastECM_CAID(&caCaid);
      keys[iidx].Get_FastECM_SID(&caSid);
      keys[iidx].Get_FastECM_PID(&caPid);

      DEBUGLOG("Init_Parity cardindex:%d iidx:%d sid:%d caSid:%d pid:%d caCaid:0x%04X caPid:%d", cardindex,iidx,sid, caSid, ipid,caCaid,caPid);

      if (sid >= 0 && caSid == sid)
      {
        keys[iidx].Init_Parity2();
        if (bdelete)
        {
          DEBUGLOG("Init_Parity delete AdapterPidMap1 cardindex:%d keyindex:%d pid:%d", cardindex, iidx, ipid);
          AdapterPidMap.erase(ipid);
          bEND = false;
          break;
        }
      }
      else if (slot >= 0 && slot == iidx)
      {
        keys[iidx].Init_Parity2();
        DEBUGLOG("Init_Parity delete AdapterPidMap2 cardindex:%d keyindex:%d pid:%d", cardindex, iidx, ipid);
        AdapterPidMap.erase(ipid);
        bEND = false;
        break;
      }
    }
  }
  while (bEND == false);

  //DebugLogPidmap();
}
#endif

int DeCSAAdapter::SearchPIDinMAP(int pid)
{
  cMutexLock lock(&mutexAdapter);
  //we must search for pid, otherwise on tune start we use always idx 0
  //int idx = -1;
  map<int, unsigned char>::iterator it;
  for (it = AdapterPidMap.begin(); it != AdapterPidMap.end(); ++it)
  {
    if (it->first == pid)
    {
      return it->second;
    }
  }

  //DEBUGLOG("%s: pid not found in m_pidmap cardindex:%d pid:%d(0x%04X) l:%d len:%d", __FUNCTION__, adapter_index,pid,pid, l,len);
  return -1;
}

void DeCSAAdapter::SetCaPid(int pid, int index)
{
  cMutexLock lock(&mutexAdapter);
  DEBUGLOG("%s: SetCaPid cardindex:%d pid:%d index:%d", __FUNCTION__, cardindex, pid, index);
  AdapterPidMap[pid] = index == -1 ? 0 : index;
}

#ifndef LIBDVBCSA
void DeCSAAdapter::SetDVBAPIPid(DeCSA* parent,int slot, int dvbapiPID)
{
  if (dvbapiPID >= 0 && slot >= 0 && slot<MAX_CSA_IDX)
  {
    cMutexLock lock(&mutexAdapter);
    int idxOK = -1;
    map<int, unsigned char>::iterator it;
    for (it = AdapterPidMap.begin(); it != AdapterPidMap.end(); ++it)
    {
      //int ipid = it->first;
      int iidx = it->second;
      if (iidx == slot)
      {
        idxOK = iidx;
        break;
      }
    }

    //slot not found - create it later in DeCSA::SetCaPid
    if (idxOK < 0)
    {
      idxOK = slot;
    }

    parent->SetFastECMPid(cardindex, idxOK, slot, dvbapiPID);
  }
  else
  {
  }
}
#endif

DeCSA::DeCSA()
{
  for (int i = 0;i < MAXADAPTER;i++)
    DeCSAArray[i].cardindex = i;

  for (int i = 0;i < MAX_CSA_IDX;i++)
    DeCSAKeyArray[i].index = i;

  ResetState();
}

DeCSA::~DeCSA()
{
}

void DeCSA::ResetState(void)
{
  DEBUGLOG("%s", __FUNCTION__);
}

bool DeCSA::GetKeyStruct(int idx)
{
  if (idx >= 0 && idx<MAX_CSA_IDX)
  {
    return DeCSAKeyArray[idx].GetorCreateKeyStruct();
  }
  return false;
}

#ifdef LIBSSL
bool DeCSA::GetKeyStructAes(int idx)
{
  if (idx >= 0)
  {
    return DeCSAKeyArray[idx].GetorCreateAesKeyStruct();
  }
  return false;
}
#endif

bool DeCSA::SetDescr(ca_descr_t *ca_descr, bool initial, int adapter_index)
{
  DEBUGLOG("%s addapter:%d", __FUNCTION__, adapter_index);

  cMutexLock lock(&mutex);

  int idx = ca_descr->index;
  if (idx < MAX_CSA_IDX && GetKeyStruct(idx))
  {
#ifndef LIBDVBCSA
    FAST_ECM fecm;
    DeCSAKeyArray[idx].Get_FastECM_struct(fecm);

    uint64_t now = GetTick();
    uint64_t evendelta = -1;
    if (fecm.evenparityTime > 0)
      evendelta = now - fecm.evenparityTime;
    uint64_t odddelta = -1;
    if (fecm.oddparityTime > 0)
      odddelta = now - fecm.oddparityTime;

    unsigned char cweven[8];
    unsigned char cwodd[8];
    DeCSAKeyArray[idx].Get_control_words(cweven, cwodd);

    DEBUGLOG("keyindex:%d adapter:%d EVENKEYOLD: CW: %02x %02x %02x %02x %02x %02x %02x %02x deltams:%lld nextparity:%d csaSid:%04x csaCaid:%04x csaPid:%04x",
      idx, adapter_index,
      cweven[0], cweven[1], cweven[2], cweven[3], cweven[4], cweven[5], cweven[6], cweven[7],
      (lldcast)evendelta, fecm.nextparity, fecm.csaSid, fecm.csaCaid, fecm.csaPid);
    DEBUGLOG("keyindex:%d adapter:%d  ODDKEYOLD: CW: %02x %02x %02x %02x %02x %02x %02x %02x deltams:%lld nextparity:%d csaSid:%04x csaCaid:%04x csaPid:%04x",
      idx, adapter_index,
      cwodd[0], cwodd[1], cwodd[2], cwodd[3], cwodd[4], cwodd[5], cwodd[6], cwodd[7],
      (lldcast)odddelta, fecm.nextparity, fecm.csaSid, fecm.csaCaid, fecm.csaPid);
#endif
    DEBUGLOG("keyindex:%d adapter:%d  %4s CW key set index:%d CW: %02x %02x %02x %02x %02x %02x %02x %02x initial:%d",
      idx, adapter_index,
      ca_descr->parity ? "odd" : "even", ca_descr->index,
      ca_descr->cw[0], ca_descr->cw[1], ca_descr->cw[2], ca_descr->cw[3], ca_descr->cw[4], ca_descr->cw[5], ca_descr->cw[6], ca_descr->cw[7],
      initial);

    DeCSAKeyArray[idx].Des_set_key(ca_descr->cw, ca_descr->parity);
    unsigned char ecm = filter->GetECM(adapter_index, ca_descr);
    if (ca_descr->parity == 0)
      DeCSAKeyArray[idx].Set_even_control_word(ca_descr->cw,ecm);
    else
      DeCSAKeyArray[idx].Set_odd_control_word(ca_descr->cw,ecm);
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
      if (DeCSAKeyArray[idx].csa_aes_key)
        AES_set_decrypt_key(ca_descr_aes->cw, 128, &((struct aes_keys_t *) DeCSAKeyArray[idx].csa_aes_key)->even);
    }
    else
    {
      if (DeCSAKeyArray[idx].csa_aes_key)
        AES_set_decrypt_key(ca_descr_aes->cw, 128, &((struct aes_keys_t *) DeCSAKeyArray[idx].csa_aes_key)->odd);
    }
  }
#endif
  return true;
}

bool DeCSA::SetData(ca_descr_data_t *ca_descr_data, bool initial)
{
  DEBUGLOG("%s", __FUNCTION__);
#ifdef LIBSSL
  cMutexLock lock(&mutex);
  int idx = ca_descr_data->index;
  if (ca_descr_data->data_type == CA_DATA_IV)
  {
    if (idx < MAX_CSA_IDX)
    {
      DEBUGLOG("%d: ivec set", idx);
      ivec[idx] = ca_descr_data->data;
    }
  }
  else if (ca_descr_data->data_type == CA_DATA_KEY)
  {
    if (idx < MAX_CSA_IDX && GetKeyStructAes(idx))
    {
      DEBUGLOG("%d: %4s aes key set", idx, ca_descr_data->parity ? "odd" : "even");
      if (ca_descr_data->parity == CA_PARITY_EVEN)
        AES_set_decrypt_key(ca_descr_data->data, 8 * ca_descr_data->length, &((struct aes_keys_t*) DeCSAKeyArray[idx].csa_aes_key)->even);
      else
        AES_set_decrypt_key(ca_descr_data->data, 8 * ca_descr_data->length, &((struct aes_keys_t*) DeCSAKeyArray[idx].csa_aes_key)->odd);
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
    if (ca_pid->index >= 0 && adapter_index>=0 && adapter_index<MAXADAPTER)
      DeCSAArray[adapter_index].SetCaPid(ca_pid->pid,ca_pid->index);
    DEBUGLOG("%d.%d: set pid 0x%04x", adapter_index, ca_pid->index, ca_pid->pid);
  }
  else
    ERRORLOG("%s: Parameter(s) out of range: adapter_index=%d, pid=0x%04x, index=0x%x", __FUNCTION__, adapter_index, ca_pid->pid, ca_pid->index);
  return true;
}

void DeCSA::SetAlgo(uint32_t index, uint32_t usedAlgo)
{
  if (index >= 0 && index < MAX_CSA_IDX)
    DeCSAKeyArray[index].SetAlgo(usedAlgo);
}

void DeCSA::SetAes(uint32_t index, bool usedAes)
{
  if (index >= 0 && index < MAX_CSA_IDX)
    DeCSAKeyArray[index].SetAes(usedAes);
}

void DeCSA::SetCipherMode(uint32_t index, uint32_t usedCipherMode)
{
  if (index < MAX_CSA_IDX)
    cipher_mode[index] = usedCipherMode;
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
  if (adapter_index < 0 || adapter_index >= MAXADAPTER)
    return false;
  return DeCSAArray[adapter_index].Decrypt(this, data, len, force);
}

#ifndef LIBDVBCSA
int DeCSA::GetCaid(uint8_t adapter_index, int pid)
{
  if (adapter_index < 0 || adapter_index >= MAXADAPTER)
    return 0;
  return DeCSAArray[adapter_index].GetCaid(this, pid);
}
#endif

void DeCSA::CancelWait()
{
  for (int i = 0;i < MAXADAPTER;i++)
    DeCSAArray[i].CancelWait();
}

#ifndef LIBDVBCSA
void DeCSA::DebugLogPidmap()
{
  if (LogLevel < 3) return;

  for (int iadapter = 0;iadapter < MAXADAPTER;iadapter++)
  {
    if (DeCSAArray[iadapter].AdapterPidMap.size() > 0)
    {
      map<int, unsigned char>::iterator it;
      for (it = DeCSAArray[iadapter].AdapterPidMap.begin(); it != DeCSAArray[iadapter].AdapterPidMap.end(); ++it)
      {
        int ipid = it->first;
        int iidx = it->second;
        FAST_ECM fecm;
        if (DeCSAKeyArray[iidx].Get_FastECM_struct(fecm))
        {
          uint64_t now = GetTick();
          uint64_t evendelta = -1;
          if (fecm.evenparityTime > 0)
            evendelta = now - fecm.evenparityTime;
          uint64_t odddelta = -1;
          if (fecm.oddparityTime > 0)
            odddelta = now - fecm.oddparityTime;

          int aparity = 0;
          int aparity2 = 0;
          DeCSAKeyArray[iidx].GetActiveParity(ipid, aparity, aparity2);

          DEBUGLOG("DebugLogPidmap cardindex:%d pid:%d(0x%04X) keyindex:%d SID:%d(0x%04X) caid:%d(0x%04X) DvbApiPid:%d(0x%04X) activparity:%d activparity2:%d nextparity:%d evendelta:%lld odddelta:%lld",
            iadapter, ipid, ipid, iidx, fecm.csaSid, fecm.csaSid, fecm.csaCaid, fecm.csaCaid, fecm.csaPid, fecm.csaPid,
            aparity, aparity2, fecm.nextparity, (lldcast)evendelta, (lldcast)odddelta);
        }
      }
    }
  }
}

void DeCSA::Init_Parity(int cardindex, int sid, int slot,bool bdelete)
{
  if (cardindex < 0)
    return;
  if (sid < 0 && slot < 0)
    return;
  DeCSAArray[cardindex].Init_Parity(DeCSAKeyArray, sid,slot,bdelete);
}

void DeCSA::SetDVBAPIPid(int adapter, int slot, int dvbapiPID)
{
  if (adapter>=0 && dvbapiPID >= 0 && slot >= 0 && slot<MAX_CSA_IDX)
    DeCSAArray[adapter].SetDVBAPIPid(this,slot, dvbapiPID);
}

void DeCSA::InitFastEcmOnCaid(int Caid)
{
  //called when timeout occurs
  //reset all stream with Caid

  //initialize disable check (wait for odd, even and so on..)
  for (int i = 0;i < MAX_CSA_IDX;i++)
    DeCSAKeyArray[i].InitFastEcmOnCaid(Caid);
}

void DeCSA::SetFastECMPid(int cardindex, int idx,int slot,int dvbapiPID)
{
  if (idx>=0 && GetKeyStruct(idx))
  {
    DEBUGLOG("SetDVBAPIPid %d.%d (PID %d (0x%04X))  keyindex:%d",cardindex, slot, dvbapiPID, dvbapiPID, idx);
    //Init_FastECM(keys[idxOK]);
    DeCSAKeyArray[idx].SetFastECMPid(dvbapiPID);
  }
}
#endif

uint32_t DeCSA::GetAlgo(int idx)
{
  if (idx < 0 || idx >= MAX_CSA_IDX)
    return -1;
  return DeCSAKeyArray[idx].GetAlgo();
}

uint32_t DeCSA::GetAes(int idx)
{
  if (idx < 0 || idx >= MAX_CSA_IDX)
    return -1;
  return DeCSAKeyArray[idx].GetAes();
}

#ifndef LIBDVBCSA
int DeCSAAdapter::GetCaid(DeCSA* parent, int pid)
{
  DEBUGLOG("%s: DeCSAAdapter::GetCaid %d", __FUNCTION__, pid);
  int ret = 0;
  if (capmt)
  {
    if (LogLevel >= 3)
    {
      map<int, unsigned char>::iterator it;
      for (it = AdapterPidMap.begin(); it != AdapterPidMap.end(); ++it)
      {
        int idx = it->second;
        if (idx >= 0)
        {
          int caCaid = 0;
          parent->DeCSAKeyArray[idx].Get_FastECM_CAID(&caCaid);
          DEBUGLOG("%s: cardindex:%d pid:%d pid:%d  keyindex:%d caid:0x%04X", __FUNCTION__, cardindex,pid,it->first,idx,caCaid);
        }
      }
    }

    int caCaid = 0;
    int idx = SearchPIDinMAP(pid);
    if (idx >= 0 && (pid < MAX_CSA_PID))
      parent->DeCSAKeyArray[idx].Get_FastECM_CAID(&caCaid);
    DEBUGLOG("%s: DeCSAAdapter::GetCaid %d 0x%04X", __FUNCTION__, pid,caCaid);
    ret = caCaid;
  }
  return ret;
}
#endif

bool DeCSAAdapter::Decrypt(DeCSA* parent, unsigned char *data, int len, bool force)
{
  cTimeMs starttime(cTimeMs::Now());

  cMutexLockHelper lockDecrypt(&mutexDecrypt);
  cMutexLockHelper lockPIDMAPnew(&mutexAdapter);

#ifndef LIBDVBCSA
  bool blogfull = false;
  uint8_t adapter_index = cardindex;
  uint64_t sleeptime = 0;
  int itimeout = 2500; //FASTECM maximum wait time for CW
  int iSleep = 50;
  int imaxSleep = itimeout / iSleep;
  cTimeMs TimerTimeout(itimeout);

  if (!rangenew)
#else
  if (!cs_tsbbatch_even || !cs_tsbbatch_odd)
#endif
  {
    ERRORLOG("%s: Error allocating memory for DeCSA", __FUNCTION__);
    return false;
  }

  int offset, currIdx = -1;
#ifndef LIBDVBCSA
  int r = -2, ccs = 0;
  bool newRange = true;
  rangenew[0] = 0;
  int curPid = 0;
#else
  int ccs = 0;
  int payload_len;
  int cs_fill_even = 0;
  int cs_fill_odd = 0;
#endif

  len -= (TS_SIZE - 1);
  int l;

  int wantsparity = 0;

  for (l = 0; l < len; l += TS_SIZE)
  {
    if (data[l] != TS_SYNC_BYTE) // let higher level cope with that
    {
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
    if (ev_od & 0x80) // encrypted
    {
      offset = ts_packet_get_payload_offset(data + l);
#ifdef LIBDVBCSA
      payload_len = TS_SIZE - offset;
#endif

      int pid = ((data[l + 1] << 8) + data[l + 2]) & MAX_CSA_PID;
      int idx = SearchPIDinMAP(pid);

      //one idx has several pids (all pids belong to channel)
      if (idx >= 0 && (pid < MAX_CSA_PID) && (currIdx < 0 || idx == currIdx)) // same or no index
      {
        currIdx = idx;
#ifndef LIBDVBCSA
        curPid = pid;
#endif
        if (ev_od == 0x80) //even
          wantsparity = 1;
        else if (ev_od == 0xC0) //odd
          wantsparity = 2;

        if (currIdx<0 || (currIdx + 1) >= MAX_CSA_IDX)
          ERRORLOG("%s: CheckExpiredCW currIdx is out of range %d", __FUNCTION__, currIdx);

        if (!parent->cipher_mode[currIdx] == CA_MODE_ECB && !parent->GetAes(currIdx) && parent->DeCSAKeyArray[currIdx].CWExpired())
          return false;

        if (parent->GetAlgo(currIdx) == CA_ALGO_DES)
        {
          if ((ev_od & 0x40) == 0)
          {
            for (int j = offset; j + 7 < 188; j += 8)
              parent->DeCSAKeyArray[currIdx].Des(&data[l + j],0);
          }
          else
          {
            for (int j = offset; j + 7 < 188; j += 8)
              parent->DeCSAKeyArray[currIdx].Des(&data[l + j], 1);
          }
          data[l + 3] &= 0x3f;    // consider it decrypted now
        }
#ifdef LIBSSL
        else if (parent->GetAlgo(currIdx) == CA_ALGO_AES128)   //extended cw mode
        {
          if (parent->cipher_mode[currIdx] == CA_MODE_ECB)
          {
            AES_KEY aes_key;
            data[l + 3] &= 0x3f;    // consider it decrypted now

            if (data[l + 3] & 0x20)
            {
              if ((188 - offset) >> 4 == 0)
                return true;
            }
            if (((ev_od & 0x40) >> 6) == 0)
              aes_key = ((struct aes_keys_t*) parent->DeCSAKeyArray[currIdx].csa_aes_key)->even;
            else
              aes_key = ((struct aes_keys_t*) parent->DeCSAKeyArray[currIdx].csa_aes_key)->odd;
            for (int j = offset; j + 16 <= 188; j += 16)
              AES_ecb_encrypt(&data[l + j], &data[l + j], &aes_key, AES_DECRYPT);
          }
          else if (parent->cipher_mode[currIdx] == CA_MODE_CBC)
          {
            AES_KEY aes_key;
            data[l + 3] &= 0x3f;    // consider it decrypted now

            if (data[l + 3] & 0x20)
            {
              if ((188 - offset) >> 4 == 0)
                return true;
            }
            if (((ev_od & 0x40) >> 6) == 0)
              aes_key = ((struct aes_keys_t*) parent->DeCSAKeyArray[currIdx].csa_aes_key)->even;
            else
              aes_key = ((struct aes_keys_t*) parent->DeCSAKeyArray[currIdx].csa_aes_key)->odd;
            for (int j = offset; j + 16 <= 188; j += 16)
              AES_cbc_encrypt(&data[l + j], &data[l + j], 16, &aes_key, parent->ivec[currIdx], AES_DECRYPT);
          }
        }
        else if (parent->GetAes(currIdx))
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
            if (parent->DeCSAKeyArray[currIdx].csa_aes_key)
              aes_key = ((struct aes_keys_t *) parent->DeCSAKeyArray[currIdx].csa_aes_key)->even;
          }
          else
          {
            if (parent->DeCSAKeyArray[currIdx].csa_aes_key)
              aes_key = ((struct aes_keys_t *) parent->DeCSAKeyArray[currIdx].csa_aes_key)->odd;
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
            rangenew[r] = &data[l];
            rangenew[r + 2] = 0;
          }
          rangenew[r + 1] = &data[l + TS_SIZE];

          if (++ccs >= csnew)
            break;
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

          if (++ccs >= cs)
            break;
#endif
        }
      }
#ifndef LIBDVBCSA
    else
      newRange = true;        // other index, create hole
#endif
    }
    else // unencrypted
    {
      // nothing, we don't create holes for unencrypted packets
    }
  }

  if (currIdx >= 0 && (parent->GetAlgo(currIdx) == CA_ALGO_DES || parent->DeCSAKeyArray[currIdx].Aes || parent->GetAlgo(currIdx) == CA_ALGO_AES128))
    return true;

#ifndef LIBDVBCSA
  if (r >= 0) // we have some range
  {
    if (ccs >= csnew || force)
    {
      if (currIdx >= 0 && parent->GetKeyStruct(currIdx))
      {
        if (wantsparity > 0)
        {
          bool bFastECM = false;
          int caCaid = -1;
          int caSid = -1;
          int caPid = -1;
          if (capmt && ENABLEFASTECM)
          {
            bool bdebuglogoCAID = false;

            parent->DeCSAKeyArray[currIdx].Get_FastECM_CAID(&caCaid);
            parent->DeCSAKeyArray[currIdx].Get_FastECM_SID(&caSid);
            parent->DeCSAKeyArray[currIdx].Get_FastECM_PID(&caPid);

            //DEBUGLOG("XXXXX CAID: %04X PID:%d currIdx:%d adapter_index:%d", caCaid, caPid, currIdx,adapter_index);
            if (caCaid <= 0)
            {
              if (caPid >= 0)
              {
                uint16_t caidneu = capmt->GetCAIDFromPid(adapter_index, caPid, caSid);
                if (caidneu > 0)
                {
                  DEBUGLOG("%s: SetFastECMCaidSid adapter:%d CAID: %04X SID: %04X parity:%d pid:%d keyindex:%d",
                    __FUNCTION__, adapter_index, caidneu, caSid, wantsparity, curPid, currIdx);
                    parent->DeCSAKeyArray[currIdx].SetFastECMCaidSid(caidneu, caSid);
                    caCaid = caidneu;
                  bdebuglogoCAID = true;
                }
              }
            }

            if (IsFastECMCAID(caCaid))
            {
              if (bdebuglogoCAID)
              {
                DEBUGLOG("%s: using Fast ECM adapter:%d CAID: %04X SID: %04X parity:%d pid:%d keyindex:%d",
                  __FUNCTION__, adapter_index, caCaid, caSid, wantsparity, curPid, currIdx);
                  parent->DebugLogPidmap();
              }
              bFastECM = true;
            }
            else //if (caCaid!=0x00)
            {
              if (bdebuglogoCAID)
              {
                DEBUGLOG("%s: not using Fast ECM adapter:%d CAID: %04X SID: %04X parity:%d pid:%d keyindex:%d",
                  __FUNCTION__, adapter_index, caCaid, caSid, wantsparity, curPid, currIdx);
                  parent->DebugLogPidmap();
              }
            }
          }

          if (bFastECM)
          {
            bool bdebugfull = false;
            if (bdebugfull)
            {
              FAST_ECM fecm;
              if (parent->DeCSAKeyArray[currIdx].Get_FastECM_struct(fecm))
              {
                int oldparity = fecm.activparity2[curPid];
                if (oldparity != wantsparity)
                {
                  DEBUGLOG("FULLDEBUG need new CW Parity - changed from old:%d new:%d pid:%d keyindex:%d adapter:%d", oldparity, wantsparity, curPid, currIdx, adapter_index);
                }
              }
            }

            bool bfirsttimecheck;
            bool bnextparityset;
            bool bactivparitypatched;

            int oldparity = 0;
            int iok = parent->DeCSAKeyArray[currIdx].Set_FastECM_CW_Parity(curPid, wantsparity, false, oldparity, bfirsttimecheck, bnextparityset, bactivparitypatched);

            if (bfirsttimecheck) DEBUGLOG("bfirsttimecheck pid:%d keyindex:%d adapter:%d", curPid, currIdx, adapter_index);
            if (bnextparityset) DEBUGLOG("bnextparityset pid:%d keyindex:%d adapter:%d", curPid, currIdx, adapter_index);
            if (bactivparitypatched) DEBUGLOG("bactivparitypatched pid:%d keyindex:%d adapter:%d", curPid, currIdx, adapter_index);

            //DEBUGLOG("%s: set_FastECM_CW_Parity ALL OK debugev_od:%d", __FUNCTION__, debugev_od);
            if (oldparity != wantsparity)
            {
                DEBUGLOG("FastECM need new CW Parity - changed from old:%d new:%d pid:%d keyindex:%d adapter:%d", oldparity, wantsparity, curPid, currIdx, adapter_index);
            }
            if (iok == 0 && bFastECM)
            {
              bCW_Waiting = true;
              cMutexLock lockstop(&mutexStopDecrypt);
              ERRORLOG("%s: set_FastECM_CW_Parity MUST WAIT parity:%d pid:%d keyindex:%d adapter:%d len:%d", __FUNCTION__,
                wantsparity, curPid, currIdx, adapter_index, len);
              parent->DebugLogPidmap();
              int isleepcount = 0;
              do
              {
                isleepcount++;
                lockPIDMAPnew.UnLock();
                cCondWait::SleepMs(iSleep);
                lockPIDMAPnew.ReLock();
                if (bAbort)
                {
                  bCW_Waiting = false;
                  bAbort = false;
                  ERRORLOG("%s: bAbort parity wait adapter:%d", __FUNCTION__, adapter_index);
                    return false;
                }
                iok = parent->DeCSAKeyArray[currIdx].Set_FastECM_CW_Parity(curPid, wantsparity, false, oldparity, bfirsttimecheck, bnextparityset, bactivparitypatched);
                if (bfirsttimecheck) DEBUGLOG("bfirsttimecheck pid:%d keyindex:%d adapter:%d", curPid, currIdx, adapter_index);
                if (bnextparityset) DEBUGLOG("bnextparityset pid:%d keyindex:%d adapter:%d", curPid, currIdx, adapter_index);
                if (bactivparitypatched) DEBUGLOG("bactivparitypatched pid:%d keyindex:%d adapter:%d", curPid, currIdx, adapter_index);
                if (iok == 1)
                {
                  sleeptime = starttime.Elapsed();
                  ERRORLOG("%s: set_FastECM_CW_Parity MUST WAIT SUCCESS parity:%d pid:%d keyindex:%d adapter:%d len:%d time:%lld", __FUNCTION__,
                    wantsparity, curPid, currIdx, adapter_index, len, (lldcast)sleeptime);
                }
                else
                {
                  if (TimerTimeout.TimedOut() || isleepcount > imaxSleep)
                  {
                    parent->DeCSAKeyArray[currIdx].Set_FastECM_CW_Parity(curPid, wantsparity, true, oldparity, bfirsttimecheck, bnextparityset, bactivparitypatched); //otherwise we sleep every time.
                    //InitFastEcmOnCaid(caCaid);
                    parent->DeCSAKeyArray[currIdx].Init_Parity2(false);
                    sleeptime = starttime.Elapsed();
                    ERRORLOG("%s: set_FastECM_CW_Parity MUST WAIT TIMEOUT parity:%d pid:%d keyindex:%d adapter:%d len:%d time:%lld", __FUNCTION__,
                      wantsparity, curPid, currIdx, adapter_index, len, (lldcast)sleeptime);
                    iok = 1;
                  }
                }
              } while (iok == 0);

              bCW_Waiting = false;
              blogfull = true;
              sleeptime = starttime.Elapsed();
            }
            else
            {
            }
          }
          else //nur ausgabe changed from usw...
          {
            FAST_ECM fecm;
            if (parent->DeCSAKeyArray[currIdx].Get_FastECM_struct(fecm))
            {
              int aparity2 = fecm.activparity2[curPid];
              int oldparity = aparity2;
              parent->DeCSAKeyArray[currIdx].SetActiveParity2(curPid,wantsparity);
              if (oldparity != wantsparity)
              {
                DEBUGLOG("need new CW Parity - changed from old:%d new:%d pid:%d keyindex:%d adapter:%d", oldparity, wantsparity, curPid, currIdx, adapter_index);
              }
            }
          }
        }

        if (rangenew)
        {
          int n = parent->DeCSAKeyArray[currIdx].Decrypt_packets(rangenew);
          if (blogfull)
          {
            DEBUGLOG("%s: n:%d adapter:%d decrypt_packets len:%d", __FUNCTION__, n, adapter_index, len);
          }

          if (n > 0)
          {
            return true;
          }
          else
          {
            DEBUGLOG("%s: decrypt_packets returns <= 0 n:%d adapter:%d parity:%d pid:%d keyindex:%d len:%d", __FUNCTION__, n, adapter_index, wantsparity, curPid, currIdx, len);
          }
        }
      }
    }
  }
#else
  if (currIdx >= 0 && wantsparity > 0 && parent->GetKeyStruct(currIdx) )
  {
    if (cs_fill_even)
    {
      cs_tsbbatch_even[cs_fill_even].data = NULL;
      dvbcsa_bs_decrypt(parent->DeCSAKeyArray[currIdx].cs_key_even, cs_tsbbatch_even, 184);
    }
    if (cs_fill_odd)
    {
      cs_tsbbatch_odd[cs_fill_odd].data = NULL;
      dvbcsa_bs_decrypt(parent->DeCSAKeyArray[currIdx].cs_key_odd, cs_tsbbatch_odd, 184);
    }
    return true;
  }
#endif
  return false;
}

void DeCSA::StopDecrypt(int adapter_index,int filter_num,int pid)
{
  if (adapter_index < 0 || adapter_index >= MAXADAPTER)
    return;

  if (DeCSAArray[adapter_index].bCW_Waiting)
  {
    DEBUGLOG("decsa CW Waiting %s", __FUNCTION__);
    DeCSAArray[adapter_index].bAbort = true;
    cMutexLock lock(&DeCSAArray[adapter_index].mutexStopDecrypt);
    DeCSAArray[adapter_index].bAbort = false;
    DEBUGLOG("decsa CW Waiting Aborted %s", __FUNCTION__);
  }
  else
  {
    //DEBUGLOG("%s: decsa ist OK", __FUNCTION__);
  }
#ifndef LIBDVBCSA
  if (filter_num==1)
  {
    DEBUGLOG("DeCSA::StopDecrypt cardindex:%d pid:%d", adapter_index, pid);
    int slot = -1;
    if (DeCSAArray[adapter_index].AdapterPidMap.size() > 0)
    {
      map<int, unsigned char>::iterator it;
      for (it = DeCSAArray[adapter_index].AdapterPidMap.begin(); it != DeCSAArray[adapter_index].AdapterPidMap.end(); ++it)
      {
        //int ipid = it->first;
        int iidx = it->second; //slotindex
        int caPid = -1;
        DeCSAKeyArray[iidx].Get_FastECM_PID(&caPid);
        if (caPid >= 0)
        {
          if (caPid == pid)
          {
            DEBUGLOG("DeCSA::StopDecrypt cardindex:%d pid:%d slot:%d", adapter_index, pid,iidx);
            slot = iidx;
            break;
          }
        }
      }
    }

    Init_Parity(adapter_index, -1,slot,false);
  }
#endif
}
