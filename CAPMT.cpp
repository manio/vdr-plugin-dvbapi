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

bool CAPMT::get_pmt(const int adapter, const int sid, unsigned char *buft)
{
  int dmxfd, count;
  int pmt_pid = 0;
  int patread = 0;
  int section_length;
  char *demux_path = NULL;
  unsigned char *buf = buft;
  struct dmx_sct_filter_params f;
  bool ret = false;
  int k;

  memset(&f, 0, sizeof(f));
  f.pid = 0;
  f.filter.filter[0] = 0x00;
  f.filter.mask[0] = 0xff;
  f.timeout = DEMUX_FILTER_TIMEOUT;
  f.flags = DMX_IMMEDIATE_START | DMX_CHECK_CRC;

  asprintf(&demux_path, "/dev/dvb/adapter%d/demux0", adapter);
  if ((dmxfd = open(demux_path, O_RDWR)) < 0)
  {
    ERRORLOG("%s: openening demux failed: %s", __FUNCTION__, strerror(errno));
    return ret;
  }
  if (demux_path)
    free(demux_path);

  if (ioctl(dmxfd, DMX_SET_FILTER, &f) == -1)
  {
    ERRORLOG("%s: ioctl DMX_SET_FILTER failed: %s", __FUNCTION__, strerror(errno));
    close(dmxfd);
    return ret;
  }

  //obtaining PMT PID
  while (!patread)
  {
    if (((count = read(dmxfd, buf, DEMUX_BUFFER_SIZE)) < 0) && errno == EOVERFLOW)
      count = read(dmxfd, buf, DEMUX_BUFFER_SIZE);
    if (count < 0)
    {
      ERRORLOG("%s: read_sections: read error: %s", __FUNCTION__, strerror(errno));
      close(dmxfd);
      return ret;
    }

    section_length = ((buf[1] & 0x0f) << 8) | buf[2];
    if (count != section_length + 3)
      continue;

    buf += 8;
    section_length -= 8;

    patread = 1;                /* assumes one section contains the whole pat */
    while (section_length > 0)
    {
      int service_id = (buf[0] << 8) | buf[1];
      if (service_id == sid)
      {
        pmt_pid = ((buf[2] & 0x1f) << 8) | buf[3];
        section_length = 0;
      }
      buf += 4;
      section_length -= 4;
    }
  }
  DEBUGLOG("%s: PMT pid=0x%X (%d)", __FUNCTION__, pmt_pid, pmt_pid);

  f.pid = pmt_pid;
  f.filter.filter[0] = 0x02;
  if (ioctl(dmxfd, DMX_SET_FILTER, &f) == -1)
  {
    ERRORLOG("%s: ioctl DMX_SET_FILTER failed: %s", __FUNCTION__, strerror(errno));
    close(dmxfd);
    return ret;
  }
  buf = buft;

  //obtaining PMT data for our SID
  for (k = 0; k < 64; k++)
  {
    if (((count = read(dmxfd, buf, DEMUX_BUFFER_SIZE)) < 0) && errno == EOVERFLOW)
      count = read(dmxfd, buf, DEMUX_BUFFER_SIZE);
    if (count < 0)
    {
      ERRORLOG("%s: read_sections: read error: %s", __FUNCTION__, strerror(errno));
      close(dmxfd);
      return ret;
    }

    section_length = ((buf[1] & 0x0f) << 8) | buf[2];
    if (count != section_length + 3)
      continue;
    else
    {
      int service_id = (buf[3] << 8) | buf[4];
      if (service_id == sid)
      {
        ret = true;
        break;
      }
    }
  }

  close(dmxfd);
  return ret;
}

CAPMT::CAPMT(void)
{
  for (int i = 0; i < MAX_SOCKETS; i++)
  {
    sids[i] = 0;
    sockets[i] = 0;
  }
}

CAPMT::~CAPMT()
{
  vector<pmtobj>::iterator it;
  for (it=pmt.begin(); it!=pmt.end(); it++)
  {
    if (it->data)
      delete[] it->data;
  }
  pmt.clear();
}

void CAPMT::ProcessSIDRequest(int card_index, int sid, int ca_lm, const unsigned char *vdr_caPMT, int vdr_caPMTLen)
{
/*
    here is what i found so far analyzing AOT_CA_PMT frame from vdr
    lm=4 prg=X: request for X SID to be decrypted (added)
    lm=5 prg=X: request for X SID to stop decryption (removed)
    lm=3 prg=0: this is sent when changing transponder or during epg scan (also before new channel but ONLY if it is on different transponder)
    lm=3 prg=X: it seems that this is sent when starting vdr with active timers
*/
  int length, offset = 0;
  if (sid == 0)
  {
    DEBUGLOG("%s: got empty SID - returning from function", __FUNCTION__);
    return;
  }

  //removind the PMT if exists
  vector<pmtobj>::iterator it;
  for (it = pmt.begin(); it != pmt.end(); it++)
  {
    if (it->sid == sid)
    {
      if (it->data)
        delete[] it->data;
      pmt.erase(it);
      break;
    }
  }
  //adding new or updating existing PMT data
  if (ca_lm == 0x04 || ca_lm == 0x03)
  {
    pmtobj pmto;
    pmto.sid = sid;
    pmto.len = vdr_caPMTLen;
    pmto.adapter = card_index;
    if (vdr_caPMTLen > 0)
    {
      pmto.pilen[0] = vdr_caPMT[4];   //reserved+program_info_length
      pmto.pilen[1] = vdr_caPMT[5];   //reserved+program_info_length (+1 for ca_pmt_cmd_id, +4 for above CAPMT_DESC_DEMUX)

      //obtaining program_info_length
      int ilen = (vdr_caPMT[4] << 8) + vdr_caPMT[5];
      //checking if we need to start copying 1 byte further and omit ca_pmt_cmd_id which we already have
      if (ilen > 0)
      {
        offset = 1;
        pmto.pilen[1] -= 1;           //-1 for ca_pmt_cmd_id which we have counted
      }

      length = vdr_caPMTLen;      //ca_pmt data length
      pmto.len = length - 6 - offset;
      unsigned char *pmt_data = new unsigned char[pmto.len];
      memcpy(pmt_data, vdr_caPMT + 6 + offset, pmto.len);    //copy ca_pmt data from vdr
      pmto.data = pmt_data;
    }
    else
    {
      static unsigned char buffer[DEMUX_BUFFER_SIZE];
      //obtain PMT data only if we don't have caDescriptors
      if (!get_pmt(card_index, sid, buffer))
      {
        ERRORLOG("Error obtaining PMT data, returning");
        return;
      }

      pmto.pilen[0] = buffer[10];     //reserved+program_info_length
      pmto.pilen[1] = buffer[11];     //reserved+program_info_length (+1 for ca_pmt_cmd_id, +4 for above CAPMT_DESC_DEMUX)

      length = ((buffer[1] & 0xf) << 8) + buffer[2] + 3;  //section_length (including 4 byte CRC)
      pmto.len = length - 12 - 4;
      unsigned char *pmt_data = new unsigned char[pmto.len];
      memcpy(pmt_data, buffer + 12, pmto.len);    //copy ca_pmt data from vdr
      pmto.data = pmt_data;
    }

    pmt.push_back(pmto);
  }

  if (pmt.empty())
  {
    if ((sockets[0]) > 0)
    {
      close (sockets[0]);
      sockets[0] = 0;
    }
  }
  else
  {
    //sending complete PMT objects
    int lm = LIST_FIRST;
    for (it = pmt.begin(); it != pmt.end();)
    {
      int sid = it->sid;
      pmtobj *pmto = &*it;
      ++it;
      if (it == pmt.end())
        lm |= LIST_LAST;
      sockets[0] = send(pmto->adapter, sid, sockets[0], lm, pmto);
      lm = LIST_MORE;
    }
  }
  initialCaDscr = true;
}

int CAPMT::oscam_socket_connect()
{
  int socket_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
  sockaddr_un serv_addr_un;
  memset(&serv_addr_un, 0, sizeof(serv_addr_un));
  serv_addr_un.sun_family = AF_LOCAL;
  snprintf(serv_addr_un.sun_path, sizeof(serv_addr_un.sun_path), "/tmp/camd.socket");
  if (connect(socket_fd, (const sockaddr *) &serv_addr_un, sizeof(serv_addr_un)) != 0)
  {
    ERRORLOG("Cannot connect to /tmp/camd.socket, Do you have OSCam running?");
    socket_fd = -1;
  }
  else
    DEBUGLOG("created socket with socket_fd=%d", socket_fd);
  return socket_fd;
}

int CAPMT::send(const int adapter, const int sid, int socket_fd, int ca_lm, const pmtobj *pmt)
{
  int length_field;
  int toWrite;

  //try to reconnect to oscam if there is no connection
  if (socket_fd == -1)
  {
    socket_fd = oscam_socket_connect();
    if (socket_fd == -1)
      return socket_fd;
  }

/////// preparing capmt data to send
  // http://cvs.tuxbox.org/lists/tuxbox-cvs-0208/msg00434.html
  DEBUGLOG("%s: channelSid=0x%x (%d)", __FUNCTION__, sid, sid);

  //ca_pmt_tag
  caPMT[0] = 0x9F;
  caPMT[1] = 0x80;
  caPMT[2] = 0x32;
  caPMT[3] = 0x82;              //2 following bytes for size

  caPMT[6] = ca_lm;             //list management
  caPMT[7] = sid >> 8;          //program_number
  caPMT[8] = sid & 0xff;        //program_number
  caPMT[9] = 0;                 //version_number, current_next_indicator

  caPMT[12] = 0x01;             //ca_pmt_cmd_id = CAPMT_CMD_OK_DESCRAMBLING
  //adding own descriptor with demux and adapter_id
  caPMT[13] = 0x82;             //CAPMT_DESC_DEMUX
  caPMT[14] = 0x02;             //length
  caPMT[15] = 0x00;             //demux id
  caPMT[16] = (char) adapter;   //adapter id

  //adding CA_PMT from vdr
  caPMT[10] = pmt->pilen[0];                  //reserved+program_info_length
  caPMT[11] = pmt->pilen[1] + 1 + 4;          //reserved+program_info_length (+1 for ca_pmt_cmd_id, +4 for above CAPMT_DESC_DEMUX)
  memcpy(caPMT + 17, pmt->data, pmt->len);    //copy ca_pmt data from vdr
  length_field = 17 + pmt->len - 6;           //-6 = 3 bytes for AOT_CA_PMT and 3 for size

  //calculating length_field()
  caPMT[4] = length_field >> 8;
  caPMT[5] = length_field & 0xff;

  //number of bytes in packet to send (adding 3 bytes of ca_pmt_tag and 3 bytes of length_field)
  toWrite = length_field + 6;

/////// sending data
  if (socket_fd == 0)
    socket_fd = oscam_socket_connect();
  if (socket_fd > 0)
  {
    int wrote = write(socket_fd, caPMT, toWrite);
    DEBUGLOG("socket_fd=%d toWrite=%d wrote=%d", socket_fd, toWrite, wrote);
    if (wrote != toWrite)
    {
      ERRORLOG("%s: wrote != toWrite", __FUNCTION__);
      close(socket_fd);
      socket_fd = 0;
    }
  }
  return socket_fd;
}
