
#ifndef ___DECSA_H
#define ___DECSA_H

#include <linux/dvb/ca.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>

#define MAX_CSA_PIDS 8192
#define MAX_CSA_IDX  16
#define MAX_STALL_MS 70

#define MAX_REL_WAIT 100 // time to wait if key in used on set
#define MAX_KEY_WAIT 500 // time to wait if key not ready on change

#define FL_EVEN_GOOD 1
#define FL_ODD_GOOD  2
#define FL_ACTIVITY  4


class DeCSA
{
private:
  int cs;
  unsigned char **range, *lastData;
  unsigned char pidmap[MAX_CSA_PIDS];
  void *keys[MAX_CSA_IDX];
  unsigned int even_odd[MAX_CSA_IDX], flags[MAX_CSA_IDX];
  cMutex mutex;
  cCondVar wait;
  cTimeMs stall;
  bool active;
  int cardindex;
  //
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
