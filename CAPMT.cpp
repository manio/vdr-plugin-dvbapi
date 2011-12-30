
#include "CAPMT.h"
#include <sys/ioctl.h>
#include <linux/dvb/dmx.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>


 int CAPMT::read_t(int fd, unsigned char *buffer)
 {
    struct pollfd p;
    p.fd = fd;
    p.events = (POLLIN | POLLPRI);
    p.revents = (POLLIN | POLLPRI);
    if (poll(&p, 1, TIMEOUT) <= 0)
    {
        esyslog("DVPAPI: CAPMT::read poll timmed");
    	return -1;
    }
    buffer[0] = 0;
    return (read(fd,&buffer[1],184));
 }


 int CAPMT::set_filter(int fd, int pid)
 {
   struct dmx_sct_filter_params flt;
   ioctl(fd, DMX_STOP);
   memset(&flt,0,sizeof(struct dmx_sct_filter_params));
   flt.timeout = 1000;
   flt.flags = DMX_IMMEDIATE_START;
   flt.pid = pid;
   if (ioctl(fd, DMX_SET_FILTER, &flt) < 0)
   {
      esyslog("DVPAPI:  CAPMT::set_filter Error in setting section filter");
      return -1;
   }
   return 0;
 }

 int CAPMT::get_pmt_pid(unsigned char *buffer, int sid)
 {
   int index=0;
   int length = (buffer[index+2] & 0x0F) << 8 | (buffer[index+3] + 3);
//   esyslog("DVPAPI: PAT length: '%d'", length);
   for (index = 9; index < length -4 ; index += 4)
       if (((buffer[index]<<8) | buffer[index+1]) > 0)
       {
          if (sid ==((buffer[index]<<8) | buffer[index+1]))
          {
             int pmt_pid = (((buffer[index+2] << 8) | buffer[index+3]) & 0x1FFF);
             esyslog("DVPAPI CAPMT::get_pmt_pid pid='0x%X (%d)'", pmt_pid, pmt_pid);
             return pmt_pid;
          }
       }
   return 0;
 }

 int CAPMT::set_filter_pmt(int fd, int pid)
 {
   struct dmx_sct_filter_params flt;
   ioctl(fd, DMX_STOP);
   memset(&flt,0,sizeof(struct dmx_sct_filter_params));
   flt.filter.filter[0] = 0x02;
   flt.filter.mask[0] = 0xff;
   flt.timeout = 1000;
   flt.flags = DMX_IMMEDIATE_START;
   flt.pid = pid;
   if (ioctl(fd, DMX_SET_FILTER, &flt) < 0)
   {
      esyslog("DVPAPI: CAPMT::set_filter_pmt Error in setting section filter");
      return -1;
   }
   return 0;
 }

bool CAPMT::get_pmt(const int adapter, const int sid, unsigned char* buffer)
{
	int fd=-1;
	int length;
	int k;
	int pmt_pid=0;
	char *demux_dev = NULL;
	bool ret = false;

	asprintf(&demux_dev, "/dev/dvb/adapter%d/demux0", adapter);
	esyslog("DVPAPI: opening demux: %s", demux_dev);
	if ((fd = open(demux_dev, O_RDWR)) < 0)
		esyslog("DVPAPI: Error opening demux device");
	else
	{
		if (set_filter(fd, 0) < 0)
			esyslog("DVPAPI: Error in set filter pat");
		if ((length = read_t(fd, buffer)) < 0)
			esyslog("DVPAPI: Error in read read_t (pat)");
		else
		{
			pmt_pid=get_pmt_pid(buffer, sid);
			if (pmt_pid==0)
				esyslog("DVPAPI: Error pmt_pid not found");
			else
			{
				if (set_filter_pmt(fd, pmt_pid) < 0)
					esyslog("DVPAPI: Error in set pmt filter");
				for (k=0; k<64; k++)
				{
					if ((length = read_t(fd, buffer)) < 0)
						esyslog("DVPAPI: Error in read pmt\n");
					if (sid==((buffer[4]<<8)+buffer[5]))
						break;
				}
				ret = true;
			}
		}
	}
	if (demux_dev)
		free(demux_dev);
	if (fd>0)
		close(fd);
	return ret;
}

// oscam also reads PMT file, but it is moch slower
//#define PMT_FILE

 int CAPMT::send(const int adapter, const int sid, int socket_fd)
 {
#ifdef PMT_FILE
   unlink("/tmp/pmt.tmp");
#endif
   int length;
//   FILE *fout;
   unsigned char buffer[4096];

	if (!get_pmt(adapter, sid, buffer))
	{
		esyslog("DVPAPI: Error obtaining PMT data, returning");
		return 0;
	}
	length=((buffer[2]&0xf)<<8) + buffer[3]+3;

#ifdef PMT_FILE
              FILE *fout=fopen("/tmp/pmt.tmp","wt");
              for (k=0;k<length;k++)
              {
                putc(buffer[k+1],fout);
              }
             fclose (fout);
#else
        char* caPMT=(char*)malloc(1024);
   // http://cvs.tuxbox.org/lists/tuxbox-cvs-0208/msg00434.html
        isyslog("DVBAPI: :: CAMPT channelSid =0x%x(%d) ",sid,sid);
        memcpy(caPMT, "\x9F\x80\x32\x82\xFF\xFB\x03\xFF\xFF\x00\x00\x13\x00", 12);
        int toWrite=(length-12-4-1)+13+2;
        caPMT[4]=(toWrite)>>8;
        caPMT[5]=(toWrite)&0xff;
        // [6]=03
        caPMT[7] = buffer[4]; // program no
        caPMT[8] = buffer[5]; // progno
        //
        caPMT[11]=buffer[12]+1;    
        caPMT[12]=0;             // demux id
        caPMT[13]=(char)adapter; // adapter id

        memcpy(caPMT+13+2,buffer+13,length-12-4-1);        

        if(socket_fd==0)
        {
        	socket_fd=socket(AF_LOCAL,SOCK_STREAM,0);
        	sockaddr_un serv_addr_un;
        	memset(&serv_addr_un,0,sizeof(serv_addr_un));
        	serv_addr_un.sun_family=AF_LOCAL;
        	snprintf(serv_addr_un.sun_path,sizeof(serv_addr_un.sun_path),"/tmp/camd.socket");
        	if(connect(socket_fd,(const sockaddr*)&serv_addr_un,sizeof(serv_addr_un))!=0)
        	{
               esyslog("DVPAPI: Canot connecto to /tmp/camd.socket, Do you have OSCam running?");
		    socket_fd=0;
        	}
		else
		    esyslog("DVPAPI: created socket with socket_fd=%d", socket_fd);
        }
        if(socket_fd!=0)
        {
          int wrote=write(socket_fd,caPMT,toWrite);
          isyslog("DVBAPI: :: CAMPT socket_fd=%d length=%d toWrite=%d wrote=%d",socket_fd,length,toWrite,wrote);
          if(wrote!=toWrite)
          {
              esyslog("DVPAPI: CAMPT:send failed");
		close(socket_fd);
		socket_fd=0;
          }
        }
        free(caPMT);
#endif
    return socket_fd;
 }

