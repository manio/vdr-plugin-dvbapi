#include <getopt.h>
#include <vdr/plugin.h>
#include <linux/dvb/ca.h>
#include "DVBAPI.h"
#include "SCDVBDevice.h"

DVBAPI::DVBAPI(void)
{
  SCDVBDevice::OnPluginLoad();
}

DVBAPI::~DVBAPI()
{
  SCDVBDevice::OnPluginUnload();
}

const char *DVBAPI::CommandLineHelp(void)
{
  return "";
}

bool DVBAPI::ProcessArgs(int argc, char *argv[])
{
  return true;
}

bool DVBAPI::Initialize(void)
{
  // Initialize any background activities the plugin shall perform.
  return SCDVBDevice::Initialize();
}

bool DVBAPI::Start(void)
{
  SCDVBDevice::Startup();
  isyslog("DVBAPI started");
  return true;
}

void DVBAPI::Stop(void)
{
  SCDVBDevice::Shutdown();
  isyslog("DVBAPI stopped");
}

void DVBAPI::Housekeeping(void)
{
  // Perform any cleanup or other regular tasks.
}

void DVBAPI::MainThreadHook(void)
{
  // Perform actions in the context of the main program thread.
  // WARNING: Use with great care - see PLUGINS.html!
}

cString DVBAPI::Active(void)
{
  // Return a message string if shutdown should be postponed
  return NULL;
}

time_t DVBAPI::WakeupTime(void)
{
  // Return custom wakeup time for shutdown script
  return 0;
}

cMenuSetupPage *DVBAPI::SetupMenu(void)
{
  // Return a setup menu in case the plugin supports one.
  return NULL;
}

bool DVBAPI::SetupParse(const char *Name, const char *Value)
{
  // Parse your own setup parameters and store their values.
  return false;
}

bool DVBAPI::Service(const char *Id, void *Data)
{
  // Handle custom service requests from other plugins
  return false;
}

const char **DVBAPI::SVDRPHelpPages(void)
{
  // Return help text for SVDRP commands this plugin implements
  return NULL;
}

cString DVBAPI::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
  // Process SVDRP commands this plugin implements
  return NULL;
}

VDRPLUGINCREATOR(DVBAPI);       // Don't touch this!
