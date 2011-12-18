

#ifndef ___CAPMT_H
#define ___CAPMT_H

#include <linux/dvb/ca.h>
#include <vdr/dvbdevice.h>
#include <vdr/thread.h>
#define DMX_FILTER_SIZE 16
/*
typedef struct dmx_filter
 {
         uint8_t  filter[DMX_FILTER_SIZE];
         uint8_t  mask[DMX_FILTER_SIZE];
         uint8_t  mode[DMX_FILTER_SIZE];
 } dmx_filter_t;


 struct dmx_sct_filter_params
 {
         uint16_t            pid;
         dmx_filter_t        filter;
         uint32_t            timeout;
         uint32_t            flags;
 #define DMX_CHECK_CRC       1
 #define DMX_ONESHOT         2
 #define DMX_IMMEDIATE_START 4
 #define DMX_KERNEL_CLIENT   0x8000
 };
 #define DMX_START                _IO('o', 41)
 #define DMX_STOP                 _IO('o', 42)
 #define DMX_SET_FILTER           _IOW('o', 43, struct dmx_sct_filter_params)
*/


#define TIMEOUT 800  // ms

class CAPMT
{
public:
   int send(const int adapter, const int sid, int socket_fd);
  private:
	 int read_t(int fd, unsigned char *buffer);
	 int set_filter_pmt(int fd, int pid);
	 int set_filter(int fd, int pid);
	 int get_pmt_pid(unsigned char *buffer, int SID);
//     void WritePmtTmp(char DvbNum, int pmtid);
};

#endif // ___DECSA_H
