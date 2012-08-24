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

#ifndef ___UDPSOCKET_H
#define ___UDPSOCKET_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <vdr/thread.h>
#include "SCCIAdapter.h"

class UDPSocket : public cThread
{
public:
  static bool bindx(SCCIAdapter *sCCIAdapter);
  static void unbind(void);
  virtual void Action(void);
  bool bint;

protected:
  UDPSocket(SCCIAdapter *sCCIAdapter);
  ~UDPSocket();

private:
  SCCIAdapter *sCCIAdapter;
  int sock;
  ca_descr_t ca_descr;
  ca_pid_t ca_pid;
};

#endif // ___UDPSOCKET_H
