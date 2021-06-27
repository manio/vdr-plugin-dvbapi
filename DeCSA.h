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

#ifndef ___DECSA_H
#define ___DECSA_H

#include <map>
#include <linux/dvb/ca.h>
#include <vdr/thread.h>

#ifdef LIBDVBCSA
extern "C" {
#include <dvbcsa/dvbcsa.h>
}
#else
#include "FFdecsa/FFdecsa.h"
#endif

#ifdef LIBSSL
#include "openssl/aes.h"
#endif

#include "DVBAPI.h"

#define MAX_CSA_PID  0x1FFF
#define MAX_CSA_IDX  32
#define MAXADAPTER   64
#define MAX_KEY_WAIT 25         // max seconds to consider a CW as valid

#include "CA.h"

using namespace std;

class DeCSA;
class DeCSAKey //Helper for FFdecsa
{
public:
  DeCSAKey();
  ~DeCSAKey();

#ifndef LIBDVBCSA
  void* key;
#else
  struct dvbcsa_bs_key_s *cs_key_even;
  struct dvbcsa_bs_key_s *cs_key_odd;
  ca_descr_t *ca_descr;
#endif

  time_t cwSeen;                // last time the CW for the related key was seen
  time_t lastcwlog;

#ifdef LIBSSL
  void *csa_aes_key;
#endif
  bool Aes;
  uint32_t algo;
  uint32_t des_key_schedule[2][32];

  int index;

  bool CWExpired(); //return true if expired
  bool GetorCreateKeyStruct();
#ifdef LIBSSL
  bool GetorCreateAesKeyStruct();
#endif
  void Des(uint8_t* data, unsigned char parity);
  void Des_set_key(const unsigned char *cw, unsigned char parity);
  bool Set_even_control_word(const unsigned char *even);
  bool Set_odd_control_word(const unsigned char *odd);

#ifndef LIBDVBCSA
  bool Get_control_words(unsigned char *even, unsigned char *odd);
  int Decrypt_packets(unsigned char **cluster);
  void SetFastECMPid(int pid);
  void Get_FastECM_CAID(int* caid);
  void Get_FastECM_SID(int* caSid);
  void Get_FastECM_PID(int* caPid);
  bool Get_FastECM_struct(FAST_ECM& fecm);
  void Init_Parity2(bool binitcsa = true);
  bool SetFastECMCaidSid(int caid, int sid);
  int Set_FastECM_CW_Parity(int pid, int parity, bool bforce, int& oldparity, bool& bfirsttimecheck, bool& bnextparityset, bool& bactivparitypatched);
  void SetActiveParity2(int pid,int parity2);
  void InitFastEcmOnCaid(int Caid);
  bool GetActiveParity(int pid, int& aparity, int& aparity2);
#endif

  void SetAes(uint32_t usedAes);
  uint32_t GetAes();

  void SetAlgo(uint32_t usedAlgo);
  uint32_t GetAlgo();

  cMutex mutexKEY;
};

class DeCSAAdapter
{
public:
  DeCSAAdapter();
  ~DeCSAAdapter();

  int cardindex;

  map<int, unsigned char> AdapterPidMap;

  void Init_Parity(DeCSAKey *keys, int sid, int slot,bool bdelete);
#ifndef LIBDVBCSA
  void SetDVBAPIPid(DeCSA* parent, int slot, int dvbapiPID);
#endif
  void SetCaPid(int pid, int index);
  int SearchPIDinMAP(int pid);
#ifndef LIBDVBCSA
  int GetCaid(DeCSA* parent, int pid);
#endif
  bool Decrypt(DeCSA* parent,unsigned char *data, int len, bool force);
  void CancelWait();

  cMutex mutexAdapter;
  cMutex mutexDecrypt;
  cMutex mutexStopDecrypt;

  int csnew;

#ifndef LIBDVBCSA
  unsigned char **rangenew;
#else
  int cs;
  struct dvbcsa_bs_batch_s *cs_tsbbatch_even;
  struct dvbcsa_bs_batch_s *cs_tsbbatch_odd;
#endif

  bool bCW_Waiting;
  bool bAbort;
};

class DeCSA
{
public:
  DeCSAAdapter DeCSAArray[MAXADAPTER];
  DeCSAKey DeCSAKeyArray[MAX_CSA_IDX]; //maximum MAX_CSA_IDX crypted channesl over all adapters
  unsigned char *ivec[MAX_CSA_IDX];
  uint32_t cipher_mode[MAX_CSA_IDX];

  cMutex mutex;
  bool GetKeyStruct(int idx);
  bool GetKeyStructAes(int idx);
  void ResetState(void);
  // to prevent copy constructor and assignment
  DeCSA(const DeCSA&);
  DeCSA& operator=(const DeCSA&);

public:
  DeCSA();
  ~DeCSA();
  bool Decrypt(uint8_t adapter_index, unsigned char *data, int len, bool force);
  bool SetDescr(ca_descr_t *ca_descr, bool initial, int adapter_index);
  bool SetDescrAes(ca_descr_aes_t *ca_descr_aes, bool initial);
  bool SetCaPid(uint8_t adapter_index, ca_pid_t *ca_pid);
  void SetAlgo(uint32_t index, uint32_t usedAlgo);
  void SetAes(uint32_t index, bool usedAes);
  void SetCipherMode(uint32_t index, uint32_t usedCipherMode);
  bool SetData(ca_descr_data_t *ca_descr_data, bool initial);

  void StopDecrypt(int adapter_index,int filter_num,int pid);
#ifndef LIBDVBCSA
  void Init_Parity(int cardindex, int sid, int slot,bool bdelete);
  void SetDVBAPIPid(int adapter, int slot, int dvbapiPID);
  void SetFastECMPid(int cardindex, int idx, int slot, int dvbapiPID);
  void DebugLogPidmap();
  void InitFastEcmOnCaid(int Caid);
  int GetCaid(uint8_t adapter_index, int pid);
#endif
  uint32_t GetAlgo(int idx);
  uint32_t GetAes(int idx);
  void CancelWait();
};

#ifndef LIBDVBCSA
extern bool IsFastECMCAID(int caCaid);
#endif
extern DeCSA *decsa;

#endif // ___DECSA_H
