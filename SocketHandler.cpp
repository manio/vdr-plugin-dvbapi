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
#include <linux/dvb/ca.h>
#include "SocketHandler.h"
#include "Log.h"

#define SOCKET_CHECK_INTERVAL   3000

SocketHandler *SockHandler = NULL;

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
  changeEndianness = false;
  protocol_version = 0;
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

    if ((rv = getaddrinfo(OSCamHost, itoa(OSCamPort), &hints, &servinfo)) != 0)
    {
      ERRORLOG("getaddrinfo error: %s", gai_strerror(rv));
      return;
    }

    // loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
      int sockfd;
      if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      {
          ERRORLOG("%s: socket error: %s", __FUNCTION__, strerror(errno));
          continue;
      }
      if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
      {
          close(sockfd);
          ERRORLOG("%s: connect error: %s", __FUNCTION__, strerror(errno));
          continue;
      }
      sock = sockfd;
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

void SocketHandler::StopDescrambling()
{
  if (protocol_version >= 1)
    SendStopDescrambling();
  else
    CloseConnection();
}

void SocketHandler::Write(unsigned char *data, int len)
{
  DEBUGLOG("%s, sock=%d", __FUNCTION__, sock);
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

  uint32_t req = htonl(DVBAPI_FILTER_DATA);               //type of request
  memcpy(&buff[0], &req, 4);
  buff[4] = demux_id;                                     //demux
  buff[5] = filter_num;                                   //filter
  memcpy(buff+6, data, len);                              //copy filter data
  SockHandler->Write(buff, sizeof(buff));
}

void SocketHandler::SendClientInfo()
{
  int len = sizeof(INFO_VERSION) - 1;                     //ignoring null termination
  unsigned char buff[7 + len];

  uint32_t req = htonl(DVBAPI_CLIENT_INFO);               //type of request
  memcpy(&buff[0], &req, 4);
  int16_t proto_version = htons(DVBAPI_PROTOCOL_VERSION); //supported protocol version
  memcpy(&buff[4], &proto_version, 2);
  buff[6] = len;
  memcpy(&buff[7], &INFO_VERSION, len);                   //copy info string
  SockHandler->Write(buff, sizeof(buff));
}

void SocketHandler::SendStopDescrambling()
{
  DEBUGLOG("%s", __FUNCTION__);
  unsigned char buff[8];

  buff[0] = 0x9F;
  buff[1] = 0x80;
  buff[2] = 0x3F;
  buff[3] = 0x04;

  buff[4] = 0x83;
  buff[5] = 0x02;
  buff[6] = 0x00;
  buff[7] = 0xFF;             //wildcard demux id

  Write(buff, 8);
}

void SocketHandler::Action(void)
{
  DEBUGLOG("%s", __FUNCTION__);
  unsigned char buff[262];
  int cRead;
  uint32_t *request;
  uint8_t adapter_index;
  int faults = 0;
  int skip_bytes = 0;
  bool new_oscam = false;

  // for the compatibility with old oscam, ask if it can use
  // the new protocol - if not, we will not try to use it later
  OpenConnection();
  SendClientInfo();
  cCondWait::SleepMs(20);
  cRead = recv(sock, &buff[0], 6, MSG_DONTWAIT);
  if (cRead == 6)
  {
    request = (uint32_t *) &buff;
    if (ntohl(*request) == DVBAPI_SERVER_INFO)
    {
      unsigned char len;

      uint16_t *proto_ver_ptr = (uint16_t *) &buff[4];
      protocol_version = ntohs(*proto_ver_ptr);

      recv(sock, &len, 1, MSG_DONTWAIT);               //string length
      cRead = recv(sock, buff+6, len, MSG_DONTWAIT);
      buff[6+len] = 0;                                 //terminate the string
      DEBUGLOG("%s: Got SERVER_INFO: %s, protocol_version = %d", __FUNCTION__, &buff[6], protocol_version);

      new_oscam = true;
      DEBUGLOG("OSCam is supporting dvbapi protocol v1 or above");
    }
  }
  if (!new_oscam)
    CloseConnection();

  while (Running())
  {
    if (sock == 0 && capmt && !capmt->Empty())
    {
      if (faults == 0 || (faults > 0 && checkTimer.TimedOut()))
      {
        DEBUGLOG("OSCam not connected, (re)connecting...");
        OpenConnection();
        if (sock > 0)
        {
          DEBUGLOG("Successfully (re)connected to OSCam");
          faults = 0;
          if (new_oscam)
            SendClientInfo();
          capmt->SendAll();
        }
        else
          faults++;
        checkTimer.Set(SOCKET_CHECK_INTERVAL);
      }
      cCondWait::SleepMs(20);
      continue;
    }

    if (protocol_version <= 0)
    {
      // first byte -> adapter_index
      cRead = recv(sock, &adapter_index, 1, MSG_DONTWAIT);
      if (cRead <= 0)
      {
        if (cRead == 0)
          CloseConnection();
        cCondWait::SleepMs(20);
        continue;
      }

      // ********* protocol-transition workaround *********
      // If we have read 0xff into adapter number, then this surely means
      // that oscam is responding to our CLIENT_INFO (using new protocol).
      // In this case we move this byte to the first position of the request,
      // and read only the 3 missing bytes
      if (adapter_index == 0xff)
      {
        buff[0] = adapter_index;
        protocol_version = 1;
        skip_bytes = 1;
      }
      else
        adapter_index -= AdapterIndexOffset;
    }

    // request
    cRead = recv(sock, &buff[skip_bytes], sizeof(int)-skip_bytes, MSG_DONTWAIT);
    if (cRead <= 0)
    {
      if (cRead == 0)
        CloseConnection();
      cCondWait::SleepMs(20);
      continue;
    }
    request = (uint32_t *) &buff;
    skip_bytes = 0;

    if (protocol_version >= 1 && ntohl(*request) != DVBAPI_SERVER_INFO)
    {
      // first byte -> adapter_index
      cRead = recv(sock, &adapter_index, 1, MSG_DONTWAIT);
      if (cRead <= 0)
      {
        if (cRead == 0)
          CloseConnection();
        cCondWait::SleepMs(20);
        continue;
      }
      adapter_index -= AdapterIndexOffset;
    }

    /* OSCam should always send in network order, but it's not fixed there so as a workaround
       probe for all possible cases here and detect when we need to change byte order.
       Possible proper values are:
         CA_SET_DESCR    0x40106f86
         CA_SET_PID      0x40086f87
         DMX_SET_FILTER  0x403c6f2b
         DMX_STOP        0x00006f2a

       Moreover the first bits of the first byte on some hardware are different.
    */
    if (((*request >> 8) & 0xffffff) == 0x866f10 ||
        ((*request >> 8) & 0xffffff) == 0x876f08 ||
        ((*request >> 8) & 0xffffff) == 0x2b6f3c ||
        ((*request >> 8) & 0xffffff) == 0x2a6f00)
    {
      //we have to change endianness
      changeEndianness = true;

      //fix 0x80 -> 0x40 and 0x20 -> 0x00 when needed
      if ((*request & 0xff) == 0x80)
        buff[0] = 0x40;
      else if ((*request & 0xff) == 0x20)
        buff[0] = 0x00;
    }

    if (protocol_version >= 1)
      *request = ntohl(*request);
    else if (changeEndianness)
      *request = htonl(*request);
    if (*request == CA_SET_PID)
      cRead = recv(sock, buff+4, sizeof(ca_pid_t), MSG_DONTWAIT);
    else if (*request == CA_SET_DESCR)
      cRead = recv(sock, buff+4, sizeof(ca_descr_t), MSG_DONTWAIT);
    else if (*request == CA_SET_DESCR_MODE)
      cRead = recv(sock, buff+4, sizeof(ca_descr_mode_t), MSG_DONTWAIT);
    else if (*request == DMX_SET_FILTER)
      cRead = recv(sock, buff+4, 2 + sizeof(struct dmx_sct_filter_params) + (protocol_version >= 1 ? -2 : 0), MSG_DONTWAIT);
    else if (*request == DMX_STOP)
      cRead = recv(sock, buff+4, 2 + 2, MSG_DONTWAIT);
    else if (*request == DVBAPI_SERVER_INFO)
    {
      unsigned char len;
      recv(sock, buff+4, 2, MSG_DONTWAIT);             //proto version
      recv(sock, &len, 1, MSG_DONTWAIT);               //string length
      cRead = recv(sock, buff+6, len, MSG_DONTWAIT);
      buff[6+len] = 0;                                 //terminate the string
    }
    else if (*request == DVBAPI_ECM_INFO)
      recv(sock, buff+4, 14, MSG_DONTWAIT);            //read ECM info const len header only
    else
    {
      ERRORLOG("%s: read failed unknown command: %08x", __FUNCTION__, *request);
      cCondWait::SleepMs(20);
      continue;
    }

    if (cRead <= 0)
    {
      if (cRead == 0)
        CloseConnection();
      cCondWait::SleepMs(20);
      continue;
    }
    if (*request == CA_SET_PID)
    {
      DEBUGLOG("%s: Got CA_SET_PID request, adapter_index=%d", __FUNCTION__, adapter_index);
      memcpy(&ca_pid, &buff[sizeof(int)], sizeof(ca_pid_t));
      if (protocol_version >= 1)
      {
        ca_pid.pid = ntohl(ca_pid.pid);
        ca_pid.index = ntohl(ca_pid.index);
      }
      else if (changeEndianness)
      {
        ca_pid.pid = htonl(ca_pid.pid);
        ca_pid.index = htonl(ca_pid.index);
      }
      decsa->SetCaPid(adapter_index, &ca_pid);
    }
    else if (*request == CA_SET_DESCR)
    {
      memcpy(&ca_descr, &buff[sizeof(int)], sizeof(ca_descr_t));
      if (protocol_version >= 1)
      {
        ca_descr.index = ntohl(ca_descr.index);
        ca_descr.parity = ntohl(ca_descr.parity);
      }
      else if (changeEndianness)
      {
        ca_descr.index = htonl(ca_descr.index);
        ca_descr.parity = htonl(ca_descr.parity);
      }
      decsa->SetDescr(&ca_descr, false);
      DEBUGLOG("%s: Got CA_SET_DESCR request, adapter_index=%d, index=%x", __FUNCTION__, adapter_index, ca_descr.index);
    }
    else if (*request == CA_SET_DESCR_MODE)
    {
      memcpy(&ca_descr_mode, &buff[sizeof(int)], sizeof(ca_descr_mode_t));
      if (protocol_version >= 1)
      {
        ca_descr_mode.index = ntohl(ca_descr_mode.index);
        ca_descr_mode.algo = (ca_descr_algo) ntohl(ca_descr_mode.algo);
      }
      else if (changeEndianness)
      {
        ca_descr_mode.index = htonl(ca_descr_mode.index);
        ca_descr_mode.algo = (ca_descr_algo) htonl(ca_descr_mode.algo);
      }
      decsa->SetAlgo(ca_descr_mode.index, ca_descr_mode.algo);
      DEBUGLOG("%s: Got CA_SET_DESCR_MODE request, adapter_index=%d, index=%x", __FUNCTION__, adapter_index, ca_descr.index);
    }
    else if (*request == DMX_SET_FILTER)
    {
      unsigned char demux_index = buff[4];
      unsigned char filter_num = buff[5];
      if (protocol_version >= 1)
      {
        int i = 6;
        uint16_t *pid_ptr = (uint16_t *) &buff[i];
        sFP2.pid = ntohs(*pid_ptr);
        i += 2;

        memcpy(&sFP2.filter.filter, &buff[i], 16);
        i += 16;
        memcpy(&sFP2.filter.mask, &buff[i], 16);
        i += 16;
        memcpy(&sFP2.filter.mode, &buff[i], 16);
        i += 16;

        uint32_t *timeout_ptr = (uint32_t *) &buff[i];
        sFP2.timeout = ntohl(*timeout_ptr);
        i += 4;

        uint32_t *flags_ptr = (uint32_t *) &buff[i];
        sFP2.flags = ntohl(*flags_ptr);
      }
      else
      {
        memcpy(&sFP2, &buff[sizeof(int) + 2], sizeof(struct dmx_sct_filter_params));
        if (changeEndianness)
        {
          sFP2.pid = htons(sFP2.pid);
          sFP2.timeout = htonl(sFP2.timeout);
          sFP2.flags = htonl(sFP2.flags);
        }
      }
      DEBUGLOG("%s: Got DMX_SET_FILTER request, adapter_index=%d, pid=%X, demux_idx=%d, filter_num=%d", __FUNCTION__, adapter_index, sFP2.pid, demux_index, filter_num);
      filter->SetFilter(adapter_index, sFP2.pid, 1, demux_index, filter_num, sFP2.filter.filter, sFP2.filter.mask);
    }
    else if (*request == DMX_STOP)
    {
      unsigned char demux_index = buff[4];
      unsigned char filter_num = buff[5];
      uint16_t pid;
      if (protocol_version >= 1)
      {
        uint16_t *pid_ptr = (uint16_t *) &buff[6];
        pid = ntohs(*pid_ptr);
      }
      else
        pid = (buff[6] << 8) + buff[7];
      DEBUGLOG("%s: Got DMX_STOP request, adapter_index=%d, pid=%X, demux_idx=%d, filter_num=%d", __FUNCTION__, adapter_index, pid, demux_index, filter_num);
      filter->SetFilter(adapter_index, pid, 0, demux_index, filter_num, NULL, NULL);
    }
    else if (*request == DVBAPI_SERVER_INFO)
    {
      uint16_t *proto_ver_ptr = (uint16_t *) &buff[4];
      protocol_version = ntohs(*proto_ver_ptr);
      DEBUGLOG("%s: Got SERVER_INFO: %s, protocol_version = %d", __FUNCTION__, &buff[6], protocol_version);
    }
    else if (*request == DVBAPI_ECM_INFO)
    {
      char cardsystem[255];
      char reader[255];
      char from[255];
      char protocol[255];
      unsigned char len, hops;
      int i = 4;

      uint16_t *sid_ptr = (uint16_t *) &buff[i];       //ServiceID
      uint16_t sid = ntohs(*sid_ptr);
      i += 2;

      uint16_t *caid_ptr = (uint16_t *) &buff[i];      //CAID
      uint16_t caid = ntohs(*caid_ptr);
      i += 2;

      uint16_t *pid_ptr = (uint16_t *) &buff[i];       //PID
      uint16_t pid = ntohs(*pid_ptr);
      i += 2;

      uint32_t *prid_ptr = (uint32_t *) &buff[i];      //ProviderID
      uint32_t prid = ntohl(*prid_ptr);
      i += 4;

      uint32_t *ecmtime_ptr = (uint32_t *) &buff[i];   //ECM time
      uint32_t ecmtime = ntohl(*ecmtime_ptr);
      i += 4;

      //cardsystem name
      recv(sock, &len, 1, MSG_DONTWAIT);               //string length
      recv(sock, cardsystem, len, MSG_DONTWAIT);
      cardsystem[len] = 0;                             //terminate the string

      //reader name
      recv(sock, &len, 1, MSG_DONTWAIT);               //string length
      recv(sock, reader, len, MSG_DONTWAIT);
      reader[len] = 0;                                 //terminate the string

      //source (from)
      recv(sock, &len, 1, MSG_DONTWAIT);               //string length
      recv(sock, from, len, MSG_DONTWAIT);
      from[len] = 0;                                   //terminate the string

      //protocol name
      recv(sock, &len, 1, MSG_DONTWAIT);               //string length
      recv(sock, protocol, len, MSG_DONTWAIT);
      protocol[len] = 0;                               //terminate the string

      recv(sock, &hops, 1, MSG_DONTWAIT);              //hops

      DEBUGLOG("%s: Got ECM_INFO: adapter_index=%d, SID = %04X, CAID = %04X (%s), PID = %04X, ProvID = %06X, ECM time = %d ms, reader = %s, from = %s, protocol = %s, hops = %d", __FUNCTION__, adapter_index, sid, caid, cardsystem, pid, prid, ecmtime, reader, from, protocol, hops);
      capmt->UpdateEcmInfo(adapter_index, sid, caid, pid, prid, ecmtime, cardsystem, reader, from, protocol, hops);
    }
    else
      DEBUGLOG("%s: Unknown request: %02X %02X %02X %02X", __FUNCTION__, request[0], request[1], request[2], request[3]);
  }
}
