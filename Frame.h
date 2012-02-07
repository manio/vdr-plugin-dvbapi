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

#ifndef ___FRAME_H
#define ___FRAME_H

#include <vdr/ringbuffer.h>
#define LEN_OFF 2

class Frame
{
private:
  cRingBufferLinear *rb;
  unsigned char *mem;
  int len, alen, glen;

public:
  Frame(void);
  ~Frame();
  void SetRb(cRingBufferLinear *Rb)
  {
    rb = Rb;
  }
  unsigned char *GetBuff(int l);
  void Put(void);
  unsigned char *Get(int &l);
  void Del(void);
  int Avail(void);
};

#endif // ___FRAME_H
