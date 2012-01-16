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
#include "SCDVBDevice.h"

class SCDVBDevice;

class UDPSocket : public cThread
{
public:
  static bool bindx(SCDVBDevice *pSCDVBDevice);
  static void unbind(void);
  virtual void Action(void);
  bool bint;

protected:
  UDPSocket(SCDVBDevice *pSCDVBDevice);
  ~UDPSocket();

private:
  SCDVBDevice *sCDVBDevice;
  int sock;
  ca_descr_t ca_descr;
  ca_pid_t ca_pid;
};

#endif // ___UDPSOCKET_H
