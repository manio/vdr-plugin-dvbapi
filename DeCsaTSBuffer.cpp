#include "DeCsaTSBuffer.h"

DeCsaTsBuffer::DeCsaTsBuffer(int File, int Size, int CardIndex, DeCSA *DeCsa, bool ScActive)
{
  SetDescription("TS buffer on device %d", CardIndex);
  f = File;
  size = Size;
  cardIndex = CardIndex;
  decsa = DeCsa;
  delivered = false;
  ringBuffer = new cRingBufferLinear(Size, TS_SIZE, true, "FFdecsa-TS");
  ringBuffer->SetTimeouts(100, 100);
  if (decsa)
    decsa->SetActive(true);
  SetActive(ScActive);
  Start();
}

DeCsaTsBuffer::~DeCsaTsBuffer()
{
  Cancel(3);
  if (decsa)
    decsa->SetActive(false);
  delete ringBuffer;
}

void DeCsaTsBuffer::SetActive(bool ScActive)
{
  scActive = ScActive;
}

void DeCsaTsBuffer::Action(void)
{
  if (ringBuffer)
  {
    bool firstRead = true;
    cPoller Poller(f);
    while (Running())
    {
      if (firstRead || Poller.Poll(100))
      {
        firstRead = false;
        int r = ringBuffer->Read(f);
        if (r < 0 && FATALERRNO)
        {
          if (errno == EOVERFLOW)
            esyslog("ERROR: driver buffer overflow on device %d", cardIndex);
          else
          {
            LOG_ERROR;
            break;
          }
        }
      }
    }
  }
}

uchar *DeCsaTsBuffer::Get(void)
{
  int Count = 0;
  if (delivered)
  {
    ringBuffer->Del(TS_SIZE);
    delivered = false;
  }
  uchar *p = ringBuffer->Get(Count);
  if (p && Count >= TS_SIZE)
  {
    if (*p != TS_SYNC_BYTE)
    {
      for (int i = 1; i < Count; i++)
        if (p[i] == TS_SYNC_BYTE && (i + TS_SIZE == Count || (i + TS_SIZE > Count && p[i + TS_SIZE] == TS_SYNC_BYTE)))
        {
          Count = i;
          break;
        }
      ringBuffer->Del(Count);
      esyslog("ERROR: skipped %d bytes to sync on TS packet on device %d", Count, cardIndex);
      return NULL;
    }

    if (scActive && (p[3] & 0xC0))
    {
      if (decsa)
      {
        if (!decsa->Decrypt(p, Count, false))
        {
          cCondWait::SleepMs(20);
          return NULL;
        }
      }
      else
        p[3] &= ~0xC0;          // FF hack
    }

    delivered = true;
    return p;
  }
  return NULL;
}
