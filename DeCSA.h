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
#define MAX_STALL_MS 70

#define MAX_REL_WAIT 100        // time to wait if key in used on set
#define MAX_KEY_WAIT 500        // time to wait if key not ready on change

#define FL_EVEN_GOOD 1
#define FL_ODD_GOOD  2
#define FL_ACTIVITY  4

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
  unsigned char pidmap[MAX_CSA_PIDS];
  unsigned int even_odd[MAX_CSA_IDX], flags[MAX_CSA_IDX];
  cMutex mutex;
  cCondVar wait;
  cTimeMs stall;
  bool active;
  int cardindex;
  bool GetKeyStruct(int idx);
  void ResetState(void);

public:
  DeCSA(int CardIndex);
  ~DeCSA();
  bool Decrypt(unsigned char *data, int len, bool force);
  bool SetDescr(ca_descr_t *ca_descr, bool initial);
  bool SetCaPid(ca_pid_t *ca_pid);
  void SetActive(bool on);
};

#endif // ___DECSA_H
