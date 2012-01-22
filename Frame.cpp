#include "Frame.h"
#include "Log.h"

Frame::Frame(void)
{
  rb = 0;
  mem = 0;
  len = alen = glen = 0;
}

Frame::~Frame()
{
  free(mem);
}

unsigned char *Frame::GetBuff(int l)
{
  if (!mem || l > alen)
  {
    free(mem);
    mem = 0;
    alen = 0;
    mem = MALLOC(unsigned char, l + LEN_OFF);
    if (mem)
      alen = l;
  }
  len = l;
  if (!mem)
  {
    ERRORLOG("ci-frame alloc failed");
    return 0;
  }
  return mem + LEN_OFF;
}

void Frame::Put(void)
{
  if (rb && mem)
  {
    *((short *) mem) = len;
    rb->Put(mem, len + LEN_OFF);
  }
}

unsigned char *Frame::Get(int &l)
{
  if (rb)
  {
    int c;
    unsigned char *data = rb->Get(c);
    if (data)
    {
      if (c > LEN_OFF)
      {
        int s = *((short *) data);
        if (c >= s + LEN_OFF)
        {
          l = glen = s;
          return data + LEN_OFF;
        }
      }
      //LDUMP(L_GEN_DEBUG,data,c,"internal: ci rb frame sync got=%d avail=%d -",c,rb->Available());
      rb->Clear();
    }
  }
  return 0;
}

int Frame::Avail(void)
{
  return rb ? rb->Available() : 0;
}

void Frame::Del(void)
{
  if (rb && glen)
  {
    rb->Del(glen + LEN_OFF);
    glen = 0;
  }
}
