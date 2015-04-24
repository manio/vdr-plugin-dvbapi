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

#ifndef ___SOCKETHANDLER_H
#define ___SOCKETHANDLER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <vdr/thread.h>
#include <linux/dvb/dmx.h>
#include "Filter.h"

#define DVBAPI_PROTOCOL_VERSION         2

#define DVBAPI_FILTER_DATA     0xFFFF0000
#define DVBAPI_CLIENT_INFO     0xFFFF0001
#define DVBAPI_SERVER_INFO     0xFFFF0002
#define DVBAPI_ECM_INFO        0xFFFF0003

#define INFO_VERSION "vdr-plugin-dvbapi " VERSION " / VDR " VDRVERSION

extern int OSCamNetworkMode;
extern char OSCamHost[HOST_NAME_MAX];
extern int OSCamPort;

class SocketHandler : public cThread
{
public:
  SocketHandler();
  ~SocketHandler();
  void OpenConnection();
  void CloseConnection();
  void StopDescrambling();
  void Write(unsigned char *data, int len);
  virtual void Action(void);
  void SendFilterData(unsigned char demux_id, unsigned char filter_num, unsigned char *data, int len);
  void SendClientInfo();
  void SendStopDescrambling();

private:
  int sock;
  cMutex mutex;
  ca_descr_t ca_descr;
  ca_pid_t ca_pid;
  dmx_sct_filter_params sFP2;
  cTimeMs checkTimer;
  bool changeEndianness;
  uint16_t protocol_version;
};

extern SocketHandler *SockHandler;

#endif // ___SOCKETHANDLER_H
