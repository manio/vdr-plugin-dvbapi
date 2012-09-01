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

#ifndef ___DECSATSBUFFER_H
#define ___DECSATSBUFFER_H

#include <linux/dvb/ca.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>
#include <vdr/ringbuffer.h>
#include "DeCSA.h"

extern int DeCsaTsBuffSize;

class DeCsaTsBuffer : public cThread
{
private:
  int f;
  int cardIndex, size;
  bool delivered;
  cRingBufferLinear *ringBuffer;
  DeCSA *decsa;
  bool scActive;
  virtual void Action(void);

public:
  DeCsaTsBuffer(int File, int Size, int CardIndex, DeCSA *DeCsa, bool ScActive);
  ~DeCsaTsBuffer();
  uchar *Get(void);
  void SetActive(bool ScActive);
};

#endif // ___DECSATSBUFFER_H
