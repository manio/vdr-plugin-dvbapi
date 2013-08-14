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
    *mem     = len & 0xff;
    *(mem+1) = len >> 8;
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
        int s = *data + (*(data+1) << 8);
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
