/* Compile as follows:
 * gcc -O -fbuiltin -fomit-frame-pointer -fPIC -shared -o dvbapi_ca.so dvbapi_ca.c -ldl
 *
 * run cccam with:
 * LD_PRELOAD=./ca.so ./xxx.i386
 *
 * Will then send CWs to vdr-sc cardclient cccam
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

//#define DEBUG

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)

#include <dlfcn.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/dvb/ca.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/ioctl.h>
#include <stdio.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <syslog.h>
#include <errno.h>

#if defined(RTLD_NEXT)
#define REAL_LIBC RTLD_NEXT
#else
#define REAL_LIBC ((void *) -1L)
#endif

#define PORT 9000						// Port starts at 9000, final port = 9000 + adapter number
#define MAX_ADAPTERS 4					// How many adapter do we have
#define MAX_CA MAX_ADAPTERS

static int cafd[MAX_CA]  = {-1,-1,-1,-1};
static int cafdc[MAX_CA] = {0,0,0,0};

#define MAX_INDEX 64
#define KEY_SIZE  8
#define INFO_SIZE (2+KEY_SIZE+KEY_SIZE)
#define EVEN_OFF  (2)
#define ODD_OFF   (2+KEY_SIZE)

static unsigned char ca_info[MAX_CA][MAX_INDEX][INFO_SIZE];


#define CA_BASE		"/dev/dvb/adapter%d/ca0"
#define CPUINFO_BASE	"/proc/cpuinfo"
#define CPUINFO_MAP	"/tmp/cpuinfo"

#define DBG(x...) {syslog(LOG_INFO, x);}
//#define DBG(x...)
#define INF(x...) {syslog(LOG_INFO, x);}
#define ERR(x...) {syslog(LOG_ERR, x);}

int proginfo=0;
#define PROGINFO() {if(!proginfo){syslog(LOG_INFO,"dvbapi_ca.so - multiple adapter support - V1.00 by Narog 3/12/2011");proginfo=1;}}

//int sendca(int fd, unsigned char *buff)
int sendcw(int fd,ca_descr_t *ca)
{
  DBG(">>>>>>>> dvbapi - sendcw - sending cw %d",sizeof(ca_descr_t));
  if ( send(fd,ca, sizeof(ca_descr_t), 0) == -1 ) {
  	ERR(">>>>>>>> dvbapi - sendcw - Send cw failed %d", errno);
    return 0;
  } // if
  return 1;
} // sendcw

static int cactl (int fd, int cai, int request, void *argp) {
  ca_descr_t *ca = (ca_descr_t *)argp;
  ca_pid_t  *cpd = (ca_pid_t  *)argp;
  switch (request) {
    case CA_SET_DESCR:
      send(fd,&request, sizeof(request), 0);
      send(fd,ca, sizeof(ca_descr_t), 0);
      DBG(">>>>>>>> dvbapi - cactl CA_SET_DESCR fd %d cai %d req %d par %d idx %d %02x%02x%02x%02x%02x%02x%02x%02x", fd, cai, request, ca->parity, ca->index, ca->cw[0], ca->cw[1], ca->cw[2], ca->cw[3], ca->cw[4], ca->cw[5], ca->cw[6], ca->cw[7]);
      if(ca->parity==0)
        memcpy(&ca_info[cai][ca->index][EVEN_OFF],ca->cw,KEY_SIZE); // even key
      else if(ca->parity==1)
        memcpy(&ca_info[cai][ca->index][ODD_OFF],ca->cw,KEY_SIZE); // odd key
      else
        ERR(">>>>>>>> dvbapi - cactl - Invalid parity %d in CA_SET_DESCR for ca id %d", ca->parity, cai);
//      send(fd, ca_info[cai][ca->index]);
//      sendcw(fd,ca);
      break;
    case CA_SET_PID:
      DBG(">>>>>>>> dvbapi - cactl - CA_SET_PID fd %d cai %d req %d (%d %04x)", fd, cai, request, cpd->index, cpd->pid);
      send(fd,&request, sizeof(request), 0);
      send(fd,cpd, sizeof(cpd), 0);
      if (cpd->index >=0 && cpd->index < MAX_INDEX) { 
        ca_info[cai][cpd->index][0] = (cpd->pid >> 0) & 0xff;
				ca_info[cai][cpd->index][1] = (cpd->pid >> 8) & 0xff;

      } else if (cpd->index == -1) {
        memset(&ca_info[cai], 0, sizeof(ca_info[cai]));
      } else
        ERR(">>>>>>>> dvbapi - cactl - Invalid index %d in CA_SET_PID (%d) for ca id %d", cpd->index, MAX_INDEX, cai);
      return 1;
    case CA_RESET:
      DBG(">>>>>>>> dvbapi - cactl - CA_RESET cai %d", cai);
      break;
    case CA_GET_CAP:
      DBG(">>>>>>>> dvbapi - cactl - CA_GET_CAP cai %d", cai);
      break;
    case CA_GET_SLOT_INFO:
      DBG(">>>>>>>> dvbapi - cactl - CA_GET_SLOT_INFO cai %d", cai);
      break;
    default:
      DBG(">>>>>>>> dvbapi - cactl - unhandled req %d cai %d", request, cai);
      //errno = EINVAL;
      //return -1;
  } // switch
  return 0;
} // cactl

/* 
    CPU Model
    Brcm4380 V4.2  // DM8000
    Brcm7401 V0.0  // DM800
    MIPS 4KEc V4.8 // DM7025
*/

static const char *cpuinfo_file = "\
system type             : ATI XILLEON HDTV SUPERTOLL\n\
processor               : 0\n\
cpu model               : Brcm4380 V4.2\n\
BogoMIPS                : 297.98\n\
wait instruction        : yes\n\
microsecond timers      : yes\n\
tlb_entries             : 16\n\
extra interrupt vector  : yes\n\
hardware watchpoint     : yes\n\
VCED exceptions         : not available\n\
VCEI exceptions         : not available\n\
";

int write_cpuinfo (void) {
  static FILE* (*func) (const char *, const char *) = NULL;
  func = (FILE* (*) (const char *, const char *)) dlsym (REAL_LIBC, "fopen");
  FILE* file = func(CPUINFO_MAP, "w");
  
  
  if(file == NULL) {
     printf(">>>>>>>> dvbapi - cpuinfo - error \"%s\" opening file\n", strerror(errno));
     return -1;
  }
  int ret = fwrite(cpuinfo_file, strlen(cpuinfo_file), 1, file);
  fclose(file);
  return 0;
}

FILE *fopen(const char *path, const char *mode){
  static FILE* (*func) (const char *, const char *) = NULL;
  write_cpuinfo();
  if (!func) func = (FILE* (*) (const char *, const char *)) dlsym (REAL_LIBC, "fopen");
  if (!strcmp(path, CPUINFO_BASE)) {
    //INF(">>>>>>>> dvbapi - fopen - %s mapped to %s", path, CPUINFO_MAP);
    return (*func) (CPUINFO_MAP, mode);
  } // if
  //DBG(">>>>>>>> dvbapi - fopen - %s", path);
  return (*func) (path, mode);
} // fopen
						  
int open (const char *pathname, int flags, ...) {
  static int (*func) (const char *, int, mode_t) = NULL;
  unsigned int adapter=0; 
  unsigned int demux=0; 
  char dest[256];
  if (!func) func = (int (*) (const char *, int, mode_t)) dlsym (REAL_LIBC, "open");
  PROGINFO();
  
  va_list args;
  mode_t mode;

  va_start (args, flags);
  mode = va_arg (args, mode_t);
  va_end (args);

	DBG(">>>>>>>> dvbapi.so - open - receive pathname=%s",pathname);

  	for(adapter = 0 ; adapter < MAX_ADAPTERS ; adapter++ ) {
  		sprintf(dest, CA_BASE, adapter);
  		if(strstr(pathname, dest)) {
    		int cai=adapter;					// cai is now based on the adapter number since each adapter only has one ca that is always ca0
    		if((cai >= 0) && (cai < MAX_ADAPTERS)) {
    			DBG(">>>>>>>> dvbapi.so - open - cafd[%d] flags %d %s (%d)", cai, flags, pathname, cafdc[cai]);
      			if(cafd[cai]==-1) {
        			cafd[cai] = socket(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
        			if(cafd[cai]==-1) {
          				ERR(">>>>>>>> dvbapi.so - open - Failed to open socket (%d)", errno);
        			} 
        			else {
	          			struct sockaddr_in saddr;
	          			fcntl(cafd[cai],F_SETFL,O_NONBLOCK);
	          			bzero(&saddr,sizeof(saddr));
	          			saddr.sin_family = AF_INET;
	          			saddr.sin_port = htons(PORT + cai);							// port = PORT + adapter number
	          			saddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	          			int r = connect(cafd[cai], (struct sockaddr *) &saddr, sizeof(saddr));
	          			if (r<0) {
    	      			  ERR(">>>>>>>> dvbapi.so - open - Failed to connect socket (%d), at localhost, port=%d", errno, PORT + cai);
	          			  close(cafd[cai]);
	          			  cafd[cai]=-1;
	          			} // if
					} // if
	      		} // if
	      		if(cafd[cai]!=-1) 
	        		cafdc[cai]++;
	      		else 
	       		 	cafdc[cai] = 0;
	      		return (cafd[cai]);
	   	 	} // if      
		} 
		// Demux removed since oscam will have direct access and we dont need to remap
	}	
	
	return (*func) (pathname, flags, mode);
} // open

int ioctl (int fd, int request, void *a) {
  static int (*func) (int, int, void *) = NULL;
  if (!func) func = (int (*) (int, int, void *)) dlsym (REAL_LIBC, "ioctl");
  int i;
  for(i=0;i<MAX_CA;i++)
    if (fd == cafd[i])
      return cactl (fd, i, request, a);
  return (*func) (fd, request, a); 
} // ioctl

int close (int fd) {
  static int (*func) (int) = NULL;
  if (!func) func = (int (*) (int)) dlsym (REAL_LIBC, "close");
  int i;
  for(i=0;i<MAX_CA;i++) {
    if (fd == cafd[i] && fd != -1) {
      DBG(">>>>>>>> dvbapi.so - close - request received to close cafd[%d] (%d) fd(%d)", i, cafdc[i],fd);
      if(--cafdc[i]) return 0;
      cafd[i] = -1;
    } // if
  } // for
  return (*func) (fd);
} // close

#endif

