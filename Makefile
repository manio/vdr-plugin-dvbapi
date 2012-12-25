#
# Makefile for a Video Disk Recorder plugin
#
# $Id: Makefile 2.13 2012/12/21 11:36:15 kls Exp $

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.

PLUGIN = dvbapi

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' DVBAPI.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The directory environment:

# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG  = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell pkg-config --variable=$(1) vdr || pkg-config --variable=$(1) ../../../vdr.pc))
LIBDIR  = $(DESTDIR)/$(call PKGCFG,libdir)
LOCDIR  = $(DESTDIR)/$(call PKGCFG,locdir)
#
TMPDIR ?= /tmp

### The compiler options:

export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

### The version number of VDR's plugin API:

APIVERSION = $(call PKGCFG,apiversion)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:

SOFILE = libvdr-$(PLUGIN).so

### Includes and Defines (add further entries here):

INCLUDES +=

DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"'

### The object files (add further files here):

OBJS = CAPMT.o DeCSA.o DeCsaTSBuffer.o dll.o DVBAPI.o DVBAPISetup.o SCDeviceProbe.o simplelist.o device.o deviceplugin.o UDPSocket.o SCCIAdapter.o Frame.o SCCAMSlot.o

# FFdeCSA
PARALLEL   ?= PARALLEL_128_SSE2
CSAFLAGS   ?= -fexpensive-optimizations -funroll-loops -mmmx -msse -msse2 -msse3
FFDECSADIR  = FFdecsa
FFDECSA     = $(FFDECSADIR)/FFdecsa.o

HAVE_SD := $(wildcard ../dvbsddevice/dvbsddevice.c)
ifneq ($(strip $(HAVE_SD)),)
  DEFINES += -DWITH_SDDVB
  DEVPLUGTARGETS += libdvbapi-dvbsddevice.so
  DEVPLUGINSTALL += install-devplug-sddvb
endif
HAVE_HD := $(wildcard ../dvbhddevice/dvbhddevice.c)
ifneq ($(strip $(HAVE_HD)),)
  HDVERS := $(shell sed -ne '/*VERSION/ s/^.*=.*"\(.*\)".*$$/\1/p' ../dvbhddevice/dvbhddevice.c)
  ifeq ($(findstring dag,$(HDVERS)),)
    DEFINES += -DWITH_HDDVB
    DEVPLUGTARGETS += libdvbapi-dvbhddevice.so
    DEVPLUGINSTALL += install-devplug-hddvb
  endif
endif
HAVE_UFS9XX := $(wildcard ../dvbufs9xx/dvbhddevice.c)
ifneq ($(strip $(HAVE_UFS9XX)),)
  DEFINES += -DWITH_UFS9XX
  DEVPLUGTARGETS += libdvbapi-dvbufs9xx.so
  DEVPLUGINSTALL += install-devplug-ufs9xx
endif

### The main target:

all: $(SOFILE) $(DEVPLUGTARGETS)

### Implicit rules:

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

### Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.cpp) > $@

-include $(DEPFILE)

### Targets:

libvdr-$(PLUGIN).so: $(OBJS) $(FFDECSA)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) $(FFDECSA) -o $@

libdvbapi-dvbsddevice.so: device-sd.o
	$(CXX) $(CXXFLAGS) -shared $< -o $@

libdvbapi-dvbhddevice.so: device-hd.o
	$(CXX) $(CXXFLAGS) -shared $< -o $@

libdvbapi-dvbufs9xxdevice.so: device-ufs9xx.o
	$(CXX) $(CXXFLAGS) -shared $< -o $@

$(FFDECSA): $(FFDECSADIR)/*.c $(FFDECSADIR)/*.h
	@$(MAKE) COMPILER="$(CXX)" FLAGS="$(CXXFLAGS) $(CSAFLAGS)" PARALLEL_MODE=$(PARALLEL) -C $(FFDECSADIR) all

install-lib: $(SOFILE)
	@install -Dm755 $^ $(LIBDIR)/$^.$(APIVERSION)

install-devplug-sddvb: libdvbapi-dvbsddevice.so
	@install -Dm755 libdvbapi-dvbsddevice.so $(LIBDIR)/libdvbapi-dvbsddevice.so.$(APIVERSION)

install-devplug-hddvb: libdvbapi-dvbhddevice.so
	@install -Dm755 libdvbapi-dvbhddevice.so $(LIBDIR)/libdvbapi-dvbsddevice.so.$(APIVERSION)

install-devplug-ufs9xx: libdvbapi-ufs9xx.so
	@install -Dm755 libdvbapi-ufs9xx.so $(LIBDIR)/libdvbapi-ufs9xx.so.$(APIVERSION)

install: install-lib $(DEVPLUGINSTALL)

dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~
	@$(MAKE) -C $(FFDECSADIR) clean
