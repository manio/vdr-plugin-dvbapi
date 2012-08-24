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

#ifndef ___DVBAPI_H
#define ___DVBAPI_H

#include <getopt.h>
#include <vdr/plugin.h>
#include "device.h"
#include "dll.h"

static const char *VERSION        = "1.0.2";
static const char *DESCRIPTION    = "DVBAPI type SOFTCAM";

class DVBAPI : public cPlugin
{
private:
  cScDlls dlls;

public:
  DVBAPI(void);
  virtual ~DVBAPI();
  virtual const char *Version(void)
  {
    return VERSION;
  }
  virtual const char *Description(void)
  {
    return DESCRIPTION;
  }
  virtual const char *CommandLineHelp(void);
  virtual bool ProcessArgs(int argc, char *argv[]);
  virtual bool Initialize(void);
  virtual bool Start(void);
  virtual void Stop(void);
  virtual void Housekeeping(void);
  virtual void MainThreadHook(void);
  virtual cString Active(void);
  virtual time_t WakeupTime(void);
  virtual const char *MainMenuEntry(void)
  {
    return NULL;
  }
  virtual cOsdObject *MainMenuAction(void)
  {
    return NULL;
  }
  virtual cMenuSetupPage *SetupMenu(void);
  virtual bool SetupParse(const char *Name, const char *Value);
  virtual bool Service(const char *Id, void *Data = NULL);
  virtual const char **SVDRPHelpPages(void);
  virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);
};

#endif // ___DVBAPI_H
