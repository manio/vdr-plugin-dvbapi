#ifndef ___FRAME_H
#define ___FRAME_H

#include <vdr/ringbuffer.h>
#define LEN_OFF 2


class Frame {
private:
  cRingBufferLinear *rb;
  unsigned char *mem;
  int len, alen, glen;
public:
  Frame(void);
  ~Frame();
  void SetRb(cRingBufferLinear *Rb) { rb=Rb; }
  unsigned char *GetBuff(int l);
  void Put(void);
  unsigned char *Get(int &l);
  void Del(void);
  int Avail(void);
  };

#endif // ___FRAME_H
