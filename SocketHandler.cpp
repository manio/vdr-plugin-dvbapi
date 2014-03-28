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
  cMutexLock lock(&mutex);

  if (OSCamNetworkMode)
  {
    // connecting via TCP socket to OSCam
    struct addrinfo hints, *servinfo, *p;
    int rv;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(OSCamHost, OSCamPort, &hints, &servinfo)) != 0)
    {
      ERRORLOG("getaddrinfo error: %s", gai_strerror(rv));
      return;
    }

    // loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
      if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      {
          ERRORLOG("%s: socket error: %s", __FUNCTION__, strerror(errno));
          continue;
      }
      if (connect(sock, p->ai_addr, p->ai_addrlen) == -1)
      {
          close(sock);
          ERRORLOG("%s: connect error: %s", __FUNCTION__, strerror(errno));
          continue;
      }
      break; // if we get here, we must have connected successfully
    }

    if (p == NULL)
    {
      // looped off the end of the list with no connection
      ERRORLOG("Cannot connect to OSCam. Check your configuration and firewall settings.");
      sock = 0;
    }

    freeaddrinfo(servinfo); // all done with this structure
  }
  else
  {
    // connecting to /tmp/camd.socket
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
  }

  if (sock)
    DEBUGLOG("created socket with socket_fd=%d", sock);
}

void SocketHandler::CloseConnection()
{
  if (sock > 0)
  {
    close(sock);
    sock = 0;
    filter->StopAllFilters();
  }
}

void SocketHandler::Write(unsigned char *data, int len)
{
  //try to reconnect to oscam if there is no connection
  if (sock == 0)
    OpenConnection();
  if (sock > 0)
  {
    int wrote = write(sock, data, len);
    DEBUGLOG("socket_fd=%d len=%d wrote=%d", sock, len, wrote);
    if (wrote != len)
    {
      ERRORLOG("%s: wrote != len", __FUNCTION__);
      close(sock);
      sock = 0;
    }
  }
}

void SocketHandler::SendFilterData(unsigned char demux_id, unsigned char filter_num, unsigned char *data, int len)
{
  unsigned char buff[6 + len];
  buff[0] = 0xff;                //data_identifier
  buff[1] = 0xff;                //data_identifier
  buff[2] = 0;                   //reserved
  buff[3] = 0;                   //reserved
  buff[4] = demux_id;            //demux
  buff[5] = filter_num;          //filter
  memcpy(buff+6, data, len);     //copy data
  SockHandler->Write(buff, sizeof(buff));
}

void SocketHandler::Action(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  unsigned char buff[sizeof(int) + 2 + sizeof(struct dmx_sct_filter_params)];
  int cRead, *request;
  uint8_t adapter_index;

  while (Running())
  {
    if (sock == 0)
    {
      if (checkTimer.TimedOut())
      {
        if (capmt && !capmt->Empty())
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
    if (cRead <= 0)
    {
      if (cRead == 0)
        sock = 0;
      cCondWait::SleepMs(20);
      continue;
    }
    request = (int *) &buff;
    if (*request == CA_SET_PID)
      cRead = recv(sock, buff+4, sizeof(ca_pid_t), MSG_DONTWAIT);
    else if (*request == CA_SET_DESCR)
      cRead = recv(sock, buff+4, sizeof(ca_descr_t), MSG_DONTWAIT);
    else if (*request == DMX_SET_FILTER)
      cRead = recv(sock, buff+4, 2 + sizeof(struct dmx_sct_filter_params), MSG_DONTWAIT);
    else if (*request == DMX_STOP)
      cRead = recv(sock, buff+4, 2 + 2, MSG_DONTWAIT);
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
    else if (*request == DMX_SET_FILTER)
    {
      unsigned char demux_index = buff[4];
      unsigned char filter_num = buff[5];
      memcpy(&sFP2, &buff[sizeof(int) + 2], sizeof(struct dmx_sct_filter_params));
      DEBUGLOG("%s: Got DMX_SET_FILTER request, adapter_index=%d, pid=%X, demux_idx=%d, filter_num=%d", __FUNCTION__, adapter_index, sFP2.pid, demux_index, filter_num);
      filter->SetFilter(adapter_index, sFP2.pid, 1, demux_index, filter_num, sFP2.filter.filter, sFP2.filter.mask);
    }
    else if (*request == DMX_STOP)
    {
      unsigned char demux_index = buff[4];
      unsigned char filter_num = buff[5];
      int pid = (buff[6] << 8) + buff[7];
      DEBUGLOG("%s: Got DMX_STOP request, adapter_index=%d, pid=%X, demux_idx=%d, filter_num=%d", __FUNCTION__, adapter_index, pid, demux_index, filter_num);
      filter->SetFilter(adapter_index, pid, 0, demux_index, filter_num, NULL, NULL);
    }
    else
      DEBUGLOG("%s: unknown request", __FUNCTION__);
  }
}
