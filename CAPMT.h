#ifndef ___CAPMT_H
#define ___CAPMT_H

#include <linux/dvb/ca.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>
#define DMX_FILTER_SIZE 16
#define TIMEOUT 800  // ms

class CAPMT
{
private:
  int read_t(int fd, unsigned char *buffer);
  int set_filter_pmt(int fd, int pid);
  int set_filter(int fd, int pid);
  int get_pmt_pid(unsigned char *buffer, int SID);
  bool get_pmt(const int adapter, const int sid, unsigned char *buffer);

public:
  int send(const int adapter, const int sid, int socket_fd, const unsigned char *vdr_caPMT, int vdr_caPMTLen);
};

#endif // ___CAPMT_H
