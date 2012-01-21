#include "DVBAPISetup.h"

int LogLevel = 2;

cMenuSetupDVBAPI::cMenuSetupDVBAPI(void)
{
  newLogLevel = LogLevel;
  Add(new cMenuEditIntItem( tr("Log level (0-3)"), &newLogLevel));
}

void cMenuSetupDVBAPI::Store(void)
{
  if (newLogLevel > 3)
    newLogLevel = 3;
  SetupStore("LogLevel", LogLevel = newLogLevel);
}
