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
#include "SocketHandler.h"
#include "CAPMT.h"
#include "Log.h"

#define SOCKET_CHECK_INTERVAL   3000

SocketHandler::~SocketHandler()
{
  Cancel(3);
  CloseConnection();
}

SocketHandler::SocketHandler()
:cThread("Socket Handler")
{
  DEBUGLOG("%s", __FUNCTION__);
  sock = 0;
  Start();
}

void SocketHandler::OpenConnection()
{
  sock = socket(AF_LOCAL, SOCK_STREAM, 0);
  sockaddr_un serv_addr_un;
  memset(&serv_addr_un, 0, sizeof(serv_addr_un));
  serv_addr_un.sun_family = AF_LOCAL;
  snprintf(serv_addr_un.sun_path, sizeof(serv_addr_un.sun_path), "/tmp/camd.socket");
  if (connect(sock, (const sockaddr *) &serv_addr_un, sizeof(serv_addr_un)) != 0)
  {
    ERRORLOG("Cannot connect to /tmp/camd.socket, Do you have OSCam running?");
    sock = 0;
  }
  else
    DEBUGLOG("created socket with socket_fd=%d", sock);
}

void SocketHandler::CloseConnection()
{
  if (sock > 0)
  {
    close(sock);
    sock = 0;
  }
}

void SocketHandler::WritePMT(unsigned char* caPMT, int toWrite)
{
  //try to reconnect to oscam if there is no connection
  if (sock == 0)
    OpenConnection();
  if (sock > 0)
  {
    int wrote = write(sock, caPMT, toWrite);
    DEBUGLOG("socket_fd=%d toWrite=%d wrote=%d", sock, toWrite, wrote);
    if (wrote != toWrite)
    {
      ERRORLOG("%s: wrote != toWrite", __FUNCTION__);
      close(sock);
      sock = 0;
    }
  }
}

void SocketHandler::Action(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  unsigned char buff[sizeof(int) + sizeof(ca_descr_t)];
  int cRead, *request;
  uint8_t adapter_index;

  while (Running())
  {
    if (sock == 0)
    {
      if (checkTimer.TimedOut())
      {
        if (!capmt->Empty())
        {
          ERRORLOG("OSCam connection lost, trying to reconnect...");
          OpenConnection();
          if (sock > 0)
            capmt->SendAll();
        }
        checkTimer.Set(SOCKET_CHECK_INTERVAL);
      }
      cCondWait::SleepMs(20);
      continue;
    }

    // first byte -> adapter_index
    cRead = recv(sock, &adapter_index, 1, MSG_DONTWAIT);
    if (cRead <= 0)
    {
      if (cRead == 0)
        sock = 0;
      cCondWait::SleepMs(20);
      continue;
    }
    // request
    cRead = recv(sock, &buff, sizeof(int), MSG_DONTWAIT);
    request = (int *) &buff;
    if (*request == CA_SET_PID)
      cRead = recv(sock, buff+4, sizeof(ca_pid_t), MSG_DONTWAIT);
    else if (*request == CA_SET_DESCR)
      cRead = recv(sock, buff+4, sizeof(ca_descr_t), MSG_DONTWAIT);
    else
    {
      ERRORLOG("%s: read failed unknown command: %s", __FUNCTION__, strerror(errno));
      cCondWait::SleepMs(20);
      continue;
    }

    if (cRead <= 0)
    {
      if (cRead == 0)
        sock = 0;
      cCondWait::SleepMs(20);
      continue;
    }
    if (*request == CA_SET_PID)
    {
      DEBUGLOG("%s: Got CA_SET_PID request, adapter_index=%d", __FUNCTION__, adapter_index);
      memcpy(&ca_pid, &buff[sizeof(int)], sizeof(ca_pid_t));
      decsa->SetCaPid(adapter_index, &ca_pid);
    }
    else if (*request == CA_SET_DESCR)
    {
      DEBUGLOG("%s: Got CA_SET_DESCR request, adapter_index=%d", __FUNCTION__, adapter_index);
      memcpy(&ca_descr, &buff[sizeof(int)], sizeof(ca_descr_t));
      decsa->SetDescr(&ca_descr, false);
    }
    else
      DEBUGLOG("%s: unknown request", __FUNCTION__);
  }
}
