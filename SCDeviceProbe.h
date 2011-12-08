#ifndef ___SCDEVICEPROBE_H
#define ___SCDEVICEPROBE_H

#define DEV_DVB_ADAPTER  "/dev/dvb/adapter"
#define DEV_DVB_FRONTEND "frontend"
#define DEV_DVB_DVR      "dvr"
#define DEV_DVB_DEMUX    "demux"
#define DEV_DVB_CA       "ca"


class SCDeviceProbe : public cDvbDeviceProbe {
private:
  static SCDeviceProbe *probe;
public:
  virtual bool Probe(int Adapter, int Frontend);
  static void Install(void);
  static void Remove(void);
  };


#endif // ___SCDEVICEPROBE_H

 
