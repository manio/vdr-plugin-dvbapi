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

#include <linux/ioctl.h>
#include "UDPSocket.h"
#include "Log.h"

static UDPSocket *me = 0;

bool UDPSocket::bindx(SCCIAdapter *sCCIAdapter)
{
  me = new UDPSocket(sCCIAdapter);
  if (me->bint)
    me->Start();
  return me->bint;
}

UDPSocket::~UDPSocket()
{
  bint = false;
  close(sock);
}

UDPSocket::UDPSocket(SCCIAdapter *sCCIAdapter)
{
  DEBUGLOG("%s", __FUNCTION__);
  this->sCCIAdapter = sCCIAdapter;
  bint = true;
}

void UDPSocket::unbind(void)
{
  delete(me);
}

void UDPSocket::Action(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  unsigned char buff[sizeof(int) + sizeof(ca_descr_t)];
  int cRead, *request;

  while (bint)
  {
    int connfd = capmt ? capmt->sockets[0] : 0;
    if (connfd == 0)
    {
      cCondWait::SleepMs(20);
      continue;
    }
    cRead = recv(connfd, &buff, sizeof(int), MSG_DONTWAIT);
    request = (int *) &buff;
    if (*request == CA_SET_PID)
      cRead = recv(connfd, buff+4, sizeof(ca_pid_t), MSG_DONTWAIT);
    else if (*request == CA_SET_DESCR)
      cRead = recv(connfd, buff+4, sizeof(ca_descr_t), MSG_DONTWAIT);
    else
    {
      ERRORLOG("%s: read failed unknown command: %s", __FUNCTION__, strerror(errno));
      cCondWait::SleepMs(20);
      continue;
    }

    if (cRead <= 0)
    {
      cCondWait::SleepMs(20);
      continue;
    }
    if (*request == CA_SET_PID)
    {
      DEBUGLOG("%s: Got CA_SET_PID request", __FUNCTION__);
      memcpy(&ca_pid, &buff[sizeof(int)], sizeof(ca_pid_t));
      decsa->SetCaPid(&ca_pid);
    }
    else if (*request == CA_SET_DESCR)
    {
      DEBUGLOG("%s: Got CA_SET_DESCR request", __FUNCTION__);
      memcpy(&ca_descr, &buff[sizeof(int)], sizeof(ca_descr_t));
      decsa->SetDescr(&ca_descr, false);
    }
    else
      DEBUGLOG("%s: unknown request", __FUNCTION__);
  }
}
