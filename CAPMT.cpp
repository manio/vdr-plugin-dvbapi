
#include "CAPMT.h"
#include <sys/ioctl.h>
#include <linux/dvb/dmx.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>


CAPMT::CAPMT(int adapter,int demux)
{
	this->adapter=adapter;
	this->demux=demux;
   camdSocket=0;
}

CAPMT::~CAPMT()
{
  if(camdSocket!=0)
    close(camdSocket);
  camdSocket=0;
}

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

// oscam also reads PMT file, but it is moch slower
//#define PMT_FILE

 void CAPMT::send(const cChannel *channel )
 {
#ifdef PMT_FILE
   unlink("/tmp/pmt.tmp");
#endif
   int fd=-1;
   int length;
   int k,ii;
   int pmt_pid=0;
//   FILE *fout;
   unsigned char buffer[4096];
   char *demux_dev;
   asprintf(&demux_dev, "/dev/dvb/adapter%d/demux%d",adapter,demux);
   esyslog("DEMUX: %s",demux_dev);
   if ((fd = open(demux_dev, O_RDWR)) < 0)
   {
      esyslog("DVPAPI: Error opening demux device");
   }
   else
   {
    if (set_filter(fd, 0) < 0)
    {
      esyslog("DVPAPI: Error in set filter pat");
    }
     if ((length = read_t(fd, buffer)) < 0)
     {
        esyslog("DVPAPI: Error in read read_t (pat)");
     }
     else
     {
       pmt_pid=get_pmt_pid(buffer, channel->Sid());
       if (pmt_pid==0)
       {
         esyslog("DVPAPI: Error pmt_pid not found");
       }
       else
       {
        if (set_filter_pmt(fd, pmt_pid) < 0)
        {
         esyslog("DVPAPI: Error in set pmt filter");
        }
        for (k=0; k<64; k++)
       {
         if ((length = read_t(fd, buffer)) < 0)
         {
           esyslog("DVPAPI: Error in read pmt\n");
         }
         if (channel->Sid()==((buffer[4]<<8)+buffer[5]))
         {
           break;
         }
        }
        
        length=((buffer[2]&0xf)<<8) + buffer[3]+3;
        close(fd);  // just in case ;)
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
        isyslog("DVBAPI: :: CAMPT channelSid =0x%x(%d) ",channel->Sid(),channel->Sid());
        memcpy(caPMT, "\x9F\x80\x32\x82\xFF\xFB\x03\xFF\xFF\x00\x00\x13\x00", 12);
        int toWrite=(length-12-4-1)+13+2;
        caPMT[4]=(toWrite)>>8;
        caPMT[5]=(toWrite)&0xff;
        // [6]=03
        caPMT[7] = buffer[4]; // program no
        caPMT[8] = buffer[5]; // progno
        //
        caPMT[11]=buffer[12]+1;    
        caPMT[12]=(char)demux; // demux id
        caPMT[13]=(char)adapter; // adapter id

        memcpy(caPMT+13+2,buffer+13,length-12-4-1);        

        if(camdSocket==0)
        {
        	camdSocket=socket(AF_LOCAL,SOCK_STREAM,0);
        	sockaddr_un serv_addr_un;
        	memset(&serv_addr_un,0,sizeof(serv_addr_un));
        	serv_addr_un.sun_family=AF_LOCAL;
        	snprintf(serv_addr_un.sun_path,sizeof(serv_addr_un.sun_path),"/tmp/camd.socket");
        	if(connect(camdSocket,(const sockaddr*)&serv_addr_un,sizeof(serv_addr_un))!=0)
        	{
               esyslog("DVPAPI: Canot connecto to /tmp/camd.socket, Do you have softcam running?");
        	   camdSocket=0;
        	}
        }
        if(camdSocket!=0)
        {
          int wrote=write(camdSocket,caPMT,toWrite);
          isyslog("DVBAPI: :: CAMPT length=%d toWrite=%d wrote=%d",length,toWrite,wrote);
          if(wrote!=toWrite)
          {
              esyslog("DVPAPI: CAMPT:send failed");
        	  close(camdSocket);
        	  camdSocket=0;
          }
        }
        free(caPMT);
#endif
         }
      }
   }
   free(demux_dev);
 }

