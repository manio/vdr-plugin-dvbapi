#ifndef ___SCDVBDEVICE_H
#define ___SCDVBDEVICE_H

#include <linux/dvb/ca.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "DeCsaTSBuffer.h"
#include "DeCSA.h"
#include "UDPSocket.h"
#include "CAPMT.h"
#include "SCCIAdapter.h"

class SCCAMSlot;
class UDPSocket;
class CADevice;
class SCCIAdapter;

#define DVB_DEV_SPEC adapter,frontend

class SCDVBDevice : public cDvbDevice
{
private:
  bool initialCaDscr;
  DeCSA *decsa;
  DeCsaTsBuffer *tsBuffer;
  cMutex switchMutex;
  cMutex tsMutex;
  SCCIAdapter *sCCIAdapter;
  int fd_dvr, fd_ca, fd_ca2;
  bool softcsa, fullts;
  cMutex cafdMutex;
  cTimeMs lastDump;
  static int budget;
  bool isReady;
  CAPMT *cAPMT;

protected:
  virtual void CiStartDecrypting(void);
  virtual bool CiAllowConcurrent(void) const;
  virtual bool SetPid(cPidHandle *Handle, int Type, bool On);
  virtual bool OpenDvr(void);
  virtual void CloseDvr(void);
  virtual bool GetTSPacket(uchar *&Data);

public:
  SCDVBDevice(int Adapter, int Frontend, int cafd);
  ~SCDVBDevice();
  virtual bool HasCi(void);
  static void Capture(void);
  static bool Initialize(void);
  static void Startup(void);
  static void Shutdown(void);
  static void SetForceBudget(int n);
  static bool ForceBudget(int n);
  virtual bool SetCaDescr(ca_descr_t *ca_descr);
  bool SoftCSA(bool live);
  void CaidsChanged(void);
  void LateInit(void);
  void EarlyShutdown(void);
  bool ScActive(void);
  static void DvbName(const char *Name, int a, int f, char *buffer, int len);
  static int DvbOpen(const char *Name, int a, int f, int Mode, bool ReportError = false);
  static void OnPluginLoad(void);
  static void OnPluginUnload(void);
  bool SetChannelDevice(const cChannel *Channel, bool LiveView);
  bool Ready(void);
  bool SetCaPid(ca_pid_t *ca_pid);
  CAPMT *GetCAPMT();
  SCCIAdapter *GetSCCIAdapter();
  void SetReady(bool Ready);
};

#endif // ___SCDVBDEVICE_H
