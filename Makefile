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
LIBDIR  = $(call PKGCFG,libdir)
LOCDIR  = $(call PKGCFG,locdir)
PLGCFG  = $(call PKGCFG,plgcfg)
#
TMPDIR ?= /tmp

### The compiler options:

export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

### Allow user defined options to overwrite defaults:

-include $(PLGCFG)

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

OBJS = CAPMT.o DeCSA.o DVBAPI.o DVBAPISetup.o SocketHandler.o SCCIAdapter.o Frame.o SCCAMSlot.o

ifndef LIBDVBCSA
# FFdeCSA
PARALLEL   ?= PARALLEL_128_SSE2
CSAFLAGS   ?= -fexpensive-optimizations -funroll-loops -mmmx -msse -msse2 -msse3
FFDECSADIR  = FFdecsa
FFDECSA     = $(FFDECSADIR)/FFdecsa.o
DECSALIB    = $(FFDECSA)
else
# libdvbcsa
DECSALIB    = -ldvbcsa
DEFINES    += -DLIBDVBCSA
endif

### The main target:

all: $(SOFILE)

### Implicit rules:

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) -o $@ $<

### Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.cpp) > $@

-include $(DEPFILE)

### Targets:

$(SOFILE): $(OBJS) $(FFDECSA)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(OBJS) $(DECSALIB) -o $@

ifndef LIBDVBCSA
$(FFDECSA): $(FFDECSADIR)/*.c $(FFDECSADIR)/*.h
	@$(MAKE) COMPILER="$(CXX)" FLAGS="$(CXXFLAGS) $(LDFLAGS) $(CSAFLAGS)" PARALLEL_MODE=$(PARALLEL) -C $(FFDECSADIR) all
endif

install-lib: $(SOFILE)
	install -D $^ $(DESTDIR)$(LIBDIR)/$^.$(APIVERSION)

install: install-lib

dist: clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean: clean-ffdecsa
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~

clean-ffdecsa:
ifndef LIBDVBCSA
	@$(MAKE) -C $(FFDECSADIR) clean
endif
