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

#include "CAPMT.h"
#include <sys/ioctl.h>
#include <linux/dvb/dmx.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include "Log.h"

int CAPMT::read_t(int fd, unsigned char *buffer)
{
  struct pollfd p;
  p.fd = fd;
  p.events = (POLLIN | POLLPRI);
  p.revents = (POLLIN | POLLPRI);
  if (poll(&p, 1, TIMEOUT) <= 0)
  {
    ERRORLOG("poll timed out");
    return -1;
  }
  buffer[0] = 0;
  return (read(fd, &buffer[1], 1024));
}

int CAPMT::set_filter(int fd, int pid)
{
  struct dmx_sct_filter_params flt;
  ioctl(fd, DMX_STOP);
  memset(&flt, 0, sizeof(struct dmx_sct_filter_params));
  flt.timeout = 1000;
  flt.flags = DMX_IMMEDIATE_START;
  flt.pid = pid;
  if (ioctl(fd, DMX_SET_FILTER, &flt) < 0)
  {
    ERRORLOG("%s: Error setting section filter", __FUNCTION__);
    return -1;
  }
  return 0;
}

int CAPMT::get_pmt_pid(unsigned char *buffer, int sid)
{
  int index = 0;
  int length = (buffer[index + 2] & 0x0F) << 8 | (buffer[index + 3] + 3);
  DEBUGLOG("%s: PAT length: '%d'", __FUNCTION__, length);
  for (index = 9; index < length - 4; index += 4)
    if (((buffer[index] << 8) | buffer[index + 1]) > 0)
    {
      if (sid == ((buffer[index] << 8) | buffer[index + 1]))
      {
        int pmt_pid = (((buffer[index + 2] << 8) | buffer[index + 3]) & 0x1FFF);
        DEBUGLOG("%s: pid=0x%X (%d)", __FUNCTION__, pmt_pid, pmt_pid);
        return pmt_pid;
      }
    }
  return 0;
}

int CAPMT::set_filter_pmt(int fd, int pid)
{
  struct dmx_sct_filter_params flt;
  ioctl(fd, DMX_STOP);
  memset(&flt, 0, sizeof(struct dmx_sct_filter_params));
  flt.filter.filter[0] = 0x02;
  flt.filter.mask[0] = 0xff;
  flt.timeout = 1000;
  flt.flags = DMX_IMMEDIATE_START;
  flt.pid = pid;
  if (ioctl(fd, DMX_SET_FILTER, &flt) < 0)
  {
    ERRORLOG("%s: Error setting section filter", __FUNCTION__);
    return -1;
  }
  return 0;
}

bool CAPMT::get_pmt(const int adapter, const int sid, unsigned char *buffer)
{
  int fd = -1;
  int length;
  int k;
  int pmt_pid = 0;
  char *demux_dev = NULL;
  bool ret = false;

  asprintf(&demux_dev, "/dev/dvb/adapter%d/demux0", adapter);
  DEBUGLOG("opening demux: %s", demux_dev);
  if ((fd = open(demux_dev, O_RDWR)) < 0)
    ERRORLOG("Error opening demux device");
  else
  {
    if (set_filter(fd, 0) < 0)
      ERRORLOG("Error in set filter pat");
    if ((length = read_t(fd, buffer)) < 0)
      ERRORLOG("Error in read read_t (pat)");
    else
    {
      pmt_pid = get_pmt_pid(buffer, sid);
      if (pmt_pid == 0)
        ERRORLOG("pmt_pid not found");
      else
      {
        if (set_filter_pmt(fd, pmt_pid) < 0)
          ERRORLOG("Error in set pmt filter");
        for (k = 0; k < 64; k++)
        {
          if ((length = read_t(fd, buffer)) < 0)
            ERRORLOG("Error in read pmt");
          if (sid == ((buffer[4] << 8) + buffer[5]))
            break;
        }
        ret = true;
      }
    }
  }
  if (demux_dev)
    free(demux_dev);
  if (fd > 0)
    close(fd);
  return ret;
}

//oscam also reads PMT file, but it is much slower
//#define PMT_FILE

int CAPMT::send(const int adapter, const int sid, int socket_fd, const unsigned char *vdr_caPMT, int vdr_caPMTLen)
{
#ifdef PMT_FILE
  unlink("/tmp/pmt.tmp");
#endif
  int length, length_field;
  int offset = 0;
  int toWrite;
  //FILE *fout;
  unsigned char buffer[4096];

  //obtain PMT data only if we don't have caDescriptors
  if (!vdr_caPMT)
  {
    if (!get_pmt(adapter, sid, buffer))
    {
      ERRORLOG("Error obtaining PMT data, returning");
      return 0;
    }
  }

#ifdef PMT_FILE
  FILE *fout = fopen("/tmp/pmt.tmp", "wt");
  for (k = 0; k < length; k++)
  {
    putc(buffer[k + 1], fout);
  }
  fclose(fout);
#else

/////// preparing capmt data to send
  char *caPMT = (char *) malloc(1024);
  if (!caPMT)
  {
    ERRORLOG("Error allocating memory for PMT data, returning");
    return 0;
  }
  // http://cvs.tuxbox.org/lists/tuxbox-cvs-0208/msg00434.html
  DEBUGLOG("channelSid=0x%x(%d)", sid, sid);

  //ca_pmt_tag
  caPMT[0] = 0x9F;
  caPMT[1] = 0x80;
  caPMT[2] = 0x32;
  caPMT[3] = 0x82;              //2 following bytes for size

  caPMT[6] = 0x03;              //list management = 3
  caPMT[7] = sid >> 8;          //program_number
  caPMT[8] = sid & 0xff;        //program_number
  caPMT[9] = 0;                 //version_number, current_next_indicator

  caPMT[12] = 0x01;             //ca_pmt_cmd_id = CAPMT_CMD_OK_DESCRAMBLING
  //adding own descriptor with demux and adapter_id
  caPMT[13] = 0x82;             //CAPMT_DESC_DEMUX
  caPMT[14] = 0x02;             //length
  caPMT[15] = 0x00;             //demux id
  caPMT[16] = (char) adapter;   //adapter id

  if (vdr_caPMT)                //adding CA_PMT from vdr
  {
    caPMT[10] = vdr_caPMT[4];   //reserved+program_info_length
    caPMT[11] = vdr_caPMT[5] + 1 + 4;   //reserved+program_info_length (+1 for ca_pmt_cmd_id, +4 for above CAPMT_DESC_DEMUX)

    //obtaining program_info_length
    int ilen = (vdr_caPMT[4] << 8) + vdr_caPMT[5];
    //checking if we need to start copying 1 byte further and omit ca_pmt_cmd_id which we already have
    if (ilen > 0)
    {
      offset = 1;
      caPMT[11] -= 1;           //-1 for ca_pmt_cmd_id which we have counted
    }

    length = vdr_caPMTLen;      //ca_pmt data length
    memcpy(caPMT + 17, vdr_caPMT + 6 + offset, length - 6 - offset);    //copy ca_pmt data from vdr

    length_field = 17 + (length - 6 - offset) - 6;      //-6 = 3 bytes for AOT_CA_PMT and 3 for size
  }
  else                          //adding full PMT data obtained from demux
  {
    caPMT[10] = buffer[11];     //reserved+program_info_length
    caPMT[11] = buffer[12] + 1 + 4;     //reserved+program_info_length (+1 for ca_pmt_cmd_id, +4 for above CAPMT_DESC_DEMUX)

    length = ((buffer[2] & 0xf) << 8) + buffer[3] + 3;  //section_length (including 4 byte CRC)
    memcpy(caPMT + 17, buffer + 13, length - 12 - 4);   //copy PMT data without (-12 bytes for header, -4 byte for CRC_32)

    length_field = 17 + (length - 12 - 4) - 6;  //-6 = 3 bytes for AOT_CA_PMT and 3 for size
  }

  //calculating length_field()
  caPMT[4] = length_field >> 8;
  caPMT[5] = length_field & 0xff;

  //number of bytes in packet to send (adding 3 bytes of ca_pmt_tag and 3 bytes of length_field)
  toWrite = length_field + 6;


/////// sending data
  if (socket_fd == 0)
  {
    socket_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    sockaddr_un serv_addr_un;
    memset(&serv_addr_un, 0, sizeof(serv_addr_un));
    serv_addr_un.sun_family = AF_LOCAL;
    snprintf(serv_addr_un.sun_path, sizeof(serv_addr_un.sun_path), "/tmp/camd.socket");
    if (connect(socket_fd, (const sockaddr *) &serv_addr_un, sizeof(serv_addr_un)) != 0)
    {
      ERRORLOG("Canot connecto to /tmp/camd.socket, Do you have OSCam running?");
      socket_fd = 0;
    }
    else
      DEBUGLOG("created socket with socket_fd=%d", socket_fd);
  }
  if (socket_fd != 0)
  {
    int wrote = write(socket_fd, caPMT, toWrite);
    DEBUGLOG("socket_fd=%d length=%d toWrite=%d wrote=%d", socket_fd, length, toWrite, wrote);
    if (wrote != toWrite)
    {
      ERRORLOG("%s: wrote != toWrite", __FUNCTION__);
      close(socket_fd);
      socket_fd = 0;
    }
  }
  free(caPMT);
#endif
  return socket_fd;
}
