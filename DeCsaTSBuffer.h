
#ifndef ___DeCsaTsBuffer_H
#define ___DeCsaTsBuffer_H

#include <linux/dvb/ca.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>
#include <vdr/ringbuffer.h>
#include "DeCSA.h"

class DeCsaTsBuffer : public cThread {
private:
  int f;
  int cardIndex, size;
  bool delivered;
  cRingBufferLinear *ringBuffer;
  //
  DeCSA *decsa;
  bool scActive;
  //
  virtual void Action(void);
public:
  DeCsaTsBuffer(int File, int Size, int CardIndex, DeCSA *DeCsa, bool ScActive);
  ~DeCsaTsBuffer();
  uchar *Get(void);
  void SetActive(bool ScActive);
  };

#endif // ___DeCsaTsBuffer_H
