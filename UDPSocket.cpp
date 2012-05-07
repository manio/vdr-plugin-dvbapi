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
  struct sockaddr_in socketAddr;
  memset(&socketAddr, 0, sizeof(sockaddr_in));
  const struct hostent *const hostaddr = gethostbyname("127.0.0.1");
  if (hostaddr)
  {
    unsigned int port;
    port = 9000 + sCCIAdapter->Adapter();
    DEBUGLOG("%s: Adapter %d\n", __FUNCTION__, sCCIAdapter->Adapter());
    DEBUGLOG("%s: hostaddr port %d", __FUNCTION__, port);
    socketAddr.sin_family = AF_INET;
    socketAddr.sin_port = htons(port);
    socketAddr.sin_addr.s_addr = ((struct in_addr *) hostaddr->h_addr)->s_addr;
    const struct protoent *const ptrp = getprotobyname("udp");
    if (ptrp)
    {
      DEBUGLOG("%s: ptrp", __FUNCTION__);
      sock = socket(PF_INET, SOCK_DGRAM, ptrp->p_proto);
      if (sock > 0)
      {
        bint = (bind(sock, (struct sockaddr *) &socketAddr, sizeof(socketAddr)) >= 0);
        if (bint >= 0)
          DEBUGLOG("%s: bint=%d", __FUNCTION__, bint);
      }
    }
  }
}

void UDPSocket::unbind(void)
{
  delete(me);
}

void UDPSocket::Action(void)
{
  DEBUGLOG("%s", __FUNCTION__);

  while (bint)
  {
    unsigned int r = 0;
    int request = 0;
    while (r < sizeof(request))
    {
      int cRead = read(sock, (&request) + r, sizeof(request));
      if (cRead <= 0)
        break;
      r = +cRead;
    }
    if (request == CA_SET_DESCR)
    {
      while (r < sizeof(ca_descr_t))
      {
        r = 0;
        int cRead = read(sock, (&ca_descr) + r, sizeof(ca_descr_t));
        if (cRead <= 0)
          break;
        r = +cRead;
      }
      if (r == sizeof(ca_descr_t))
      {
        DEBUGLOG("%s: Got CA_SET_DESCR request", __FUNCTION__);
        sCCIAdapter->DeCSASetCaDescr(&ca_descr);
      }
    }
    if (request == CA_SET_PID)
    {
      r = 0;
      while (r < sizeof(ca_pid_t))
      {
        int cRead = read(sock, (&ca_pid) + r, sizeof(ca_pid_t));
        if (cRead <= 0)
          break;
        r = +cRead;
      }
      if (r == sizeof(ca_pid_t))
      {
        DEBUGLOG("%s: Got CA_SET_PID request", __FUNCTION__);
        sCCIAdapter->DeCSASetCaPid(&ca_pid);
      }
    }
  }
}
