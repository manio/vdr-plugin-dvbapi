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

#ifndef ___DEVICEPLUGIN_H
#define ___DEVICEPLUGIN_H

#include "simplelist.h"
#include "SCDeviceProbe.h"

class cScDevicePlugin : public cSimpleItem
{
public:
  void SetInitialCaDescr();
  cScDevicePlugin(void);
  virtual ~cScDevicePlugin();
  virtual cDevice *Probe(int Adapter, int Frontend, uint32_t SubSystemId) = 0;
  virtual bool LateInit(cDevice *dev) = 0;
  virtual bool EarlyShutdown(cDevice *dev) = 0;
};

extern cSimpleList<cScDevicePlugin> devplugins;

#endif // ___DEVICEPLUGIN_H
