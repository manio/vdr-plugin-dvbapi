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

#include "dll.h"
#include "Log.h"

// --- cScDll ------------------------------------------------------------------

cScDll::cScDll(const char *FileName)
{
  fileName = strdup(FileName);
  handle = 0;
}

cScDll::~cScDll()
{
  if (handle)
    dlclose(handle);
  free(fileName);
}

bool cScDll::Load(bool check)
{
  char *base = rindex(fileName, '/');
  if (base)
    base++;
  else
    base = fileName;
  INFOLOG("loading library: %s", base);
  if (!handle)
  {
    handle = dlopen(fileName, RTLD_NOW | (check ? RTLD_LOCAL : RTLD_GLOBAL));
    if (handle)
      return true;
    else
      ERRORLOG("dload: %s: %s", base, dlerror());
  }
  return false;
}

// --- cScDlls -----------------------------------------------------------------

cScDlls::cScDlls(void)
{
  handle = 0;
}

cScDlls::~cScDlls()
{
  Clear();
  if (handle)
    dlclose(handle);
  DEBUGLOG("unload done");
}

bool cScDlls::Load(void)
{
  Dl_info info;
  static int marker = 0;
  if (!dladdr((void *) &marker, &info))
  {
    ERRORLOG("dladdr: %s", dlerror());
    return false;
  }

  // we have to re-dlopen our selfs as VDR doesn't use RTLD_GLOBAL
  // but our symbols have to be available to the sub libs.
  handle = dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL);
  if (!handle)
  {
    ERRORLOG("dlopen myself: %s", dlerror());
    return false;
  }

  char *path = strdup(info.dli_fname);
  char *p;
  if ((p = rindex(path, '/')))
    *p = 0;
  DEBUGLOG("library path %s", path);

  char pat[64];
#ifdef WITH_SDDVB
  {
    snprintf(pat, sizeof(pat), "%s%s%s%s", LIBVDR_PREFIX, "dvbsddevice", SO_INDICATOR, APIVERSION);
    cScDll *dll = new cScDll(AddDirectory(path, pat));
    if (dll)
    {
      if (dll->Load(false))
      {
        INFOLOG("VDR dvbsddevice plugin loaded as subplugin");
      }
      Ins(dll);
    }
  }
#endif //WITH_SDDVB
#ifdef WITH_HDDVB
  {
    snprintf(pat, sizeof(pat), "%s%s%s%s", LIBVDR_PREFIX, "dvbhddevice", SO_INDICATOR, APIVERSION);
    cScDll *dll = new cScDll(AddDirectory(path, pat));
    if (dll)
    {
      if (dll->Load(false))
      {
        INFOLOG("VDR dvbhddevice plugin loaded as subplugin");
      }
      Ins(dll);
    }
  }
#endif //WITH_HDDVB

  snprintf(pat, sizeof(pat), "%s*%s%s", LIBDVBAPI_PREFIX, SO_INDICATOR, APIVERSION);
  bool res = true;
  cReadDir dir(path);
  struct dirent *e;
  while ((e = dir.Next()))
  {
    if (!fnmatch(pat, e->d_name, FNM_PATHNAME | FNM_NOESCAPE))
    {
      cScDll *dll = new cScDll(AddDirectory(path, e->d_name));
      if (dll)
      {
        if (!dll->Load(true))
          res = false;
        Ins(dll);
      }
    }
  }
  free(path);
  return res;
}
