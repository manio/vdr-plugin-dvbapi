#ifndef ___SCCAMSLOT_H
#define ___SCCAMSLOT_H

#include "SCCIAdapter.h"
#include "Frame.h"

#define SLOT_CAID_CHECK 10000
#define SLOT_RESET_TIME 600
#define MAX_CW_IDX        16
#define CAID_TIME      300000 // time between caid scans
#define TRIGGER_TIME    10000 // min. time between caid scan trigger
#define MAX_SOCKETS        16 // max sockets (simultaneus channels) per demux

#ifdef VDR_MAXCAID
#define MAX_CI_SLOT_CAIDS VDR_MAXCAID
#else
#define MAX_CI_SLOT_CAIDS 16
#endif

/*
#define TDPU_SIZE_INDICATOR 0x80
struct TPDU {
  unsigned char slot;
  unsigned char tcid;
  unsigned char tag;
  unsigned char len;
  unsigned char data[1];
  };
*/
class SCCIAdapter;



class SCCAMSlot : public cCamSlot {
private:
  SCCIAdapter *sCCIAdapter;
  unsigned short caids[MAX_CI_SLOT_CAIDS+1];
  int slot, cardIndex, version;
  cTimeMs checkTimer;
  bool reset, doReply;
  cTimeMs resetTimer;
  eModuleStatus lastStatus;
  cRingBufferLinear rb;
  Frame frame;
public:
  SCCAMSlot(SCCIAdapter *ca, int cardIndex, int slot);

  int GetLength(const unsigned char * &data);
  int LengthSize(int n);
  void SetSize(int n, unsigned char * &p);
  void CaInfo(int tcid, int cid);
  bool Check(void);
  void Process(const unsigned char *data, int len);
  eModuleStatus Status(void);
  bool Reset(bool log=true);
  Frame* getFrame(void) { return &frame; }
  };

#endif // ___SCCAMSLOT_H
