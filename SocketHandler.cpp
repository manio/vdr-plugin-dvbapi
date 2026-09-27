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
#include <poll.h>
#include "SocketHandler.h"
#include "Log.h"

#define SOCKET_CHECK_INTERVAL   3000
// TCP may deliver a message in several segments: how long to wait for the rest
#define SOCKET_RECEIVE_TIMEOUT  2000

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
  sock = -1;
  protocol_version = 0;
  Start();
}

void SocketHandler::OpenConnection()
{
  cMutexLock lock(&mutex);

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
    sock = -1;
  }

  freeaddrinfo(servinfo); // all done with this structure

  if (sock >= 0)
    DEBUGLOG("created socket with socket_fd=%d", sock.load());
}

void SocketHandler::CloseConnection()
{
  // under the mutex, so that a Write() on another thread never uses the fd
  // after it was closed (or reused)
  int fd;
  {
    cMutexLock lock(&mutex);
    fd = sock;
    sock = -1;
  }
  if (fd >= 0)
  {
    close(fd);
    filter->StopAllFilters();
  }
}

void SocketHandler::Write(const unsigned char *data, int len)
{
  // callers run on several threads: a message must go out whole before the
  // next one starts
  cMutexLock lock(&mutex);
  int fd = sock;
  DEBUGLOG("%s, sock=%d", __FUNCTION__, fd);
  if (fd < 0)
    return;
  while (len > 0)
  {
    ssize_t wrote = send(fd, data, len, MSG_NOSIGNAL);
    if (wrote < 0 && errno == EINTR)
      continue;
    if (wrote <= 0)
    {
      ERRORLOG("%s: write to OSCam failed: %s", __FUNCTION__, wrote < 0 ? strerror(errno) : "no progress");
      // the socket thread may be reading the fd, so only it closes it; the
      // shutdown wakes it up to reconnect
      shutdown(fd, SHUT_RDWR);
      return;
    }
    data += wrote;
    len -= wrote;
  }
}

bool SocketHandler::Receive(void *buf, int len)
{
  unsigned char *p = (unsigned char *) buf;
  while (len > 0)
  {
    ssize_t n = recv(sock, p, len, MSG_DONTWAIT);
    if (n > 0)
    {
      p += n;
      len -= n;
      continue;
    }
    if (n == 0)
      return false;             // closed by OSCam
    if (errno == EINTR)
      continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      return false;
    struct pollfd pfd = { sock, POLLIN, 0 };
    int r = poll(&pfd, 1, SOCKET_RECEIVE_TIMEOUT);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0)
    {
      ERRORLOG("%s: incomplete message from OSCam", __FUNCTION__);
      return false;
    }
  }
  return true;
}

bool SocketHandler::ReceiveString(char *str)
{
  unsigned char len;
  if (!Receive(&len, 1) || !Receive(str, len))
    return false;
  str[len] = 0;
  return true;
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
  int faults = 0;

  while (Running())
  {
    if (sock < 0)
    {
      if (capmt && !capmt->Empty() && (faults == 0 || checkTimer.TimedOut()))
      {
        DEBUGLOG("OSCam not connected, (re)connecting...");
        OpenConnection();
        if (sock >= 0)
        {
          DEBUGLOG("Successfully (re)connected to OSCam");
          faults = 0;
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

    // wait for the next message; the timeout keeps Running() checked
    struct pollfd pfd = { sock, POLLIN, 0 };
    int r = poll(&pfd, 1, 100);
    if (r == 0 || (r < 0 && errno == EINTR))
      continue;

    // a partially read message would leave the stream out of step
    uint32_t request;
    uint8_t adapter_index = 0;
    if (r < 0 || !Receive(&request, sizeof(request)))
    {
      CloseConnection();
      continue;
    }
    request = ntohl(request);
    if (request != DVBAPI_SERVER_INFO)
    {
      if (!Receive(&adapter_index, 1))
      {
        CloseConnection();
        continue;
      }
      adapter_index -= AdapterIndexOffset;
    }
    if (!HandleMessage(request, adapter_index))
      CloseConnection();
  }
}

bool SocketHandler::HandleMessage(uint32_t request, uint8_t adapter_index)
{
  unsigned char buff[262];

  if (request == CA_SET_PID)
  {
    ca_pid_t ca_pid;
    if (!Receive(&ca_pid, sizeof(ca_pid_t)))
      return false;
    DEBUGLOG("%s: Got CA_SET_PID request, adapter_index=%d", __FUNCTION__, adapter_index);
    ca_pid.pid = ntohl(ca_pid.pid);
    ca_pid.index = ntohl(ca_pid.index);
    decsa->SetCaPid(adapter_index, &ca_pid);
  }
  else if (request == CA_SET_DESCR)
  {
    ca_descr_t ca_descr;
    if (!Receive(&ca_descr, sizeof(ca_descr_t)))
      return false;
    ca_descr.index = ntohl(ca_descr.index);
    ca_descr.parity = ntohl(ca_descr.parity);
    decsa->SetAes(ca_descr.index, false);
    decsa->SetDescr(&ca_descr, false, adapter_index);
    DEBUGLOG("%s: Got CA_SET_DESCR request, adapter_index=%d, index=%x", __FUNCTION__, adapter_index, ca_descr.index);
  }
  else if (request == CA_SET_DESCR_AES)
  {
    ca_descr_aes_t ca_descr_aes;
    if (!Receive(&ca_descr_aes, sizeof(ca_descr_aes_t)))
      return false;
    ca_descr_aes.index = ntohl(ca_descr_aes.index);
    ca_descr_aes.parity = ntohl(ca_descr_aes.parity);
    decsa->SetAes(ca_descr_aes.index, true);
    decsa->SetDescrAes(&ca_descr_aes, false);
    DEBUGLOG("%s: Got CA_SET_DESCR_AES request, adapter_index=%d, index=%x", __FUNCTION__, adapter_index, ca_descr_aes.index);
  }
  else if (request == CA_SET_DESCR_MODE)
  {
    ca_descr_mode_t ca_descr_mode;
    if (!Receive(&ca_descr_mode, sizeof(ca_descr_mode_t)))
      return false;
    ca_descr_mode.index = ntohl(ca_descr_mode.index);
    ca_descr_mode.algo = (ca_descr_algo) ntohl(ca_descr_mode.algo);
    ca_descr_mode.cipher_mode = (ca_descr_cipher_mode) ntohl(ca_descr_mode.cipher_mode);
    decsa->SetAlgo(ca_descr_mode.index, ca_descr_mode.algo);
    decsa->SetCipherMode(ca_descr_mode.index, ca_descr_mode.cipher_mode);
    DEBUGLOG("%s: Got CA_SET_DESCR_MODE request, adapter_index=%d, index=%x", __FUNCTION__, adapter_index, ca_descr_mode.index);
  }
  else if (request == CA_SET_DESCR_DATA)
  {
    // on the wire: index, parity, data_type, length, then length bytes of data
    // (ca_descr_data_t itself ends in a pointer, so its size says nothing)
    uint32_t header[4];
    if (!Receive(header, sizeof(header)))
      return false;
    ca_descr_data_t ca_descr_data;
    ca_descr_data.index = ntohl(header[0]);
    ca_descr_data.parity = (ca_descr_parity) ntohl(header[1]);
    ca_descr_data.data_type = (ca_descr_data_type) ntohl(header[2]);
    ca_descr_data.length = ntohl(header[3]);
    if (ca_descr_data.length > sizeof(buff))
    {
      ERRORLOG("%s: CA_SET_DESCR_DATA length %u out of range", __FUNCTION__, ca_descr_data.length);
      return false;
    }
    if (!Receive(buff, ca_descr_data.length))
      return false;
    ca_descr_data.data = buff;
    decsa->SetAes(ca_descr_data.index, false);
    decsa->SetData(&ca_descr_data, false);
    DEBUGLOG("%s: Got CA_SET_DESCR_DATA request, adapter_index=%d, index=%x", __FUNCTION__, adapter_index, ca_descr_data.index);
  }
  else if (request == DMX_SET_FILTER)
  {
    // demux, filter number, pid, filter/mask/mode (16 each), timeout, flags
    if (!Receive(buff, 2 + 2 + 3 * DMX_FILTER_SIZE + 4 + 4))
      return false;
    unsigned char demux_index = buff[0];
    unsigned char filter_num = buff[1];
    int i = 2;

    uint16_t pid;
    memcpy(&pid, &buff[i], 2);
    pid = ntohs(pid);
    i += 2;

    unsigned char *flt = &buff[i];
    i += DMX_FILTER_SIZE;
    unsigned char *mask = &buff[i];
    // mode, timeout and flags are not used

    DEBUGLOG("%s: Got DMX_SET_FILTER request, adapter_index=%d, pid=%X, demux_idx=%d, filter_num=%d", __FUNCTION__, adapter_index, pid, demux_index, filter_num);
    filter->SetFilter(adapter_index, pid, 1, demux_index, filter_num, flt, mask);
  }
  else if (request == DMX_STOP)
  {
    if (!Receive(buff, 2 + 2))
      return false;
    unsigned char demux_index = buff[0];
    unsigned char filter_num = buff[1];

    uint16_t pid;
    memcpy(&pid, &buff[2], 2);
    pid = ntohs(pid);

    DEBUGLOG("%s: Got DMX_STOP request, adapter_index=%d, pid=%X, demux_idx=%d, filter_num=%d", __FUNCTION__, adapter_index, pid, demux_index, filter_num);
    filter->SetFilter(adapter_index, pid, 0, demux_index, filter_num, NULL, NULL);
  }
  else if (request == DVBAPI_SERVER_INFO)
  {
    uint16_t proto_ver;
    char info[256];
    if (!Receive(&proto_ver, 2) || !ReceiveString(info))
      return false;
    protocol_version = ntohs(proto_ver);
    DEBUGLOG("%s: Got SERVER_INFO: %s, protocol_version = %d", __FUNCTION__, info, protocol_version);
  }
  else if (request == DVBAPI_ECM_INFO)
  {
    // one length byte each, so 255 characters + terminator at most
    char cardsystem[256];
    char reader[256];
    char from[256];
    char protocol[256];
    int8_t hops;

    uint16_t sid, caid, pid;
    uint32_t prid, ecmtime;
    if (!Receive(&sid, 2) || !Receive(&caid, 2) || !Receive(&pid, 2) || !Receive(&prid, 4) || !Receive(&ecmtime, 4) ||
        !ReceiveString(cardsystem) || !ReceiveString(reader) || !ReceiveString(from) || !ReceiveString(protocol) ||
        !Receive(&hops, 1))
      return false;
    sid = ntohs(sid);
    caid = ntohs(caid);
    pid = ntohs(pid);
    prid = ntohl(prid);
    ecmtime = ntohl(ecmtime);

    DEBUGLOG("%s: Got ECM_INFO: adapter_index=%d, SID = %04X, CAID = %04X (%s), PID = %04X, ProvID = %06X, ECM time = %d ms, reader = %s, from = %s, protocol = %s, hops = %d", __FUNCTION__, adapter_index, sid, caid, cardsystem, pid, prid, ecmtime, reader, from, protocol, hops);
    capmt->UpdateEcmInfo(adapter_index, sid, caid, pid, prid, ecmtime, cardsystem, reader, from, protocol, hops);
  }
  else
  {
    // the length is unknown, so the stream cannot be resynchronized
    ERRORLOG("%s: read failed unknown command: %08x", __FUNCTION__, request);
    return false;
  }
  return true;
}
