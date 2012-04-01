#
# Makefile for a Video Disk Recorder plugin
#
# $Id: Makefile,v 1.2 2006/07/05 20:19:56 thomas Exp $

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.
# IMPORTANT: the presence of this macro is important for the Make.config
# file. So it must be defined, even if it is not used here!
#
PLUGIN = dvbapi

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' DVBAPI.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The C++ compiler and options:

CXX      ?= g++
CXXFLAGS ?= -march=athlon64 -fPIC -O2 -Wall -Woverloaded-virtual

### The directory environment:

VDRDIR = ../../..
LIBDIR = ../../lib
TMPDIR = /tmp

### Make sure that necessary options are included:

-include $(VDRDIR)/Make.global

### Allow user defined options to overwrite defaults:

-include $(VDRDIR)/Make.config

### The version number of VDR's plugin API (taken from VDR's "config.h"):

APIVERSION = $(shell sed -ne '/define APIVERSION/s/^.*"\(.*\)".*$$/\1/p' $(VDRDIR)/config.h)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### Includes and Defines (add further entries here):

INCLUDES += -I$(VDRDIR)/include

DEFINES += -D_GNU_SOURCE -DPLUGIN_NAME_I18N='"$(PLUGIN)"'

### The object files (add further files here):

OBJS = CAPMT.o DeCSA.o DeCsaTSBuffer.o DVBAPI.o DVBAPISetup.o SCDeviceProbe.o SCDVBDevice.o UDPSocket.o SCCIAdapter.o Frame.o SCCAMSlot.o

# FFdeCSA
CPUOPT     ?= athlon64
PARALLEL   ?= PARALLEL_128_SSE
CSAFLAGS   ?= -fPIC -O3 -fexpensive-optimizations -funroll-loops -mmmx -msse -msse2 -msse3
FFDECSADIR  = FFdecsa
FFDECSA     = $(FFDECSADIR)/FFdecsa.o
FFDECSATEST = $(FFDECSADIR)/FFdecsa_test.done

### The main target:

all: libvdr-$(PLUGIN).so

### Implicit rules:

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

### Targets:

libvdr-$(PLUGIN).so: $(OBJS) $(FFDECSA)
	$(CXX) $(CXXFLAGS) -shared $(OBJS) $(FFDECSA) -o $@
	@cp --remove-destination $@ $(LIBDIR)/$@.$(APIVERSION)

$(FFDECSA): $(FFDECSADIR)/*.c $(FFDECSADIR)/*.h
	@$(MAKE) COMPILER="$(CXX)" FLAGS="$(CSAFLAGS) -march=$(CPUOPT)" PARALLEL_MODE=$(PARALLEL) -C $(FFDECSADIR) all

dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) --exclude debian --exclude CVS --exclude .svn $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS)  $(DEPFILE) *.so *.tgz core* *~ $(PODIR)/*.mo $(PODIR)/*.pot
	@$(MAKE) -C $(FFDECSADIR) clean
