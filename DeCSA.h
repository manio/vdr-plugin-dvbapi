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

#include <linux/dvb/ca.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>

#ifdef LIBDVBCSA
extern "C" {
#include <dvbcsa/dvbcsa.h>
}
#endif

#define MAX_CSA_PIDS 8192
#define MAX_CSA_IDX  16
#define MAX_ADAPTERS 4
#define MAX_KEY_WAIT 20         // max seconds to consider a CW as valid

extern bool CheckExpiredCW;

class DeCSA
{
private:
  int cs;
#ifndef LIBDVBCSA
  unsigned char **range, *lastData;
  void *keys[MAX_CSA_IDX];
#else
  struct dvbcsa_bs_batch_s *cs_tsbbatch_even;
  struct dvbcsa_bs_batch_s *cs_tsbbatch_odd;
  struct dvbcsa_bs_key_s *cs_key_even[MAX_CSA_IDX];
  struct dvbcsa_bs_key_s *cs_key_odd[MAX_CSA_IDX];
#endif
  time_t cwSeen[MAX_CSA_IDX];   // last time the CW for the related key was seen
  unsigned char pidmap[MAX_ADAPTERS][MAX_CSA_PIDS];    //FIXME: change to dynamic structure
  cMutex mutex;
  int cardindex;
  bool GetKeyStruct(int idx);
  void ResetState(void);
  // to prevent copy constructor and assignment
  DeCSA(const DeCSA&);
  DeCSA& operator=(const DeCSA&);

public:
  DeCSA(int CardIndex);
  ~DeCSA();
  bool Decrypt(uint8_t adapter_index, unsigned char *data, int len, bool force);
  bool SetDescr(ca_descr_t *ca_descr, bool initial);
  bool SetCaPid(uint8_t adapter_index, ca_pid_t *ca_pid);
};

#endif // ___DECSA_H
