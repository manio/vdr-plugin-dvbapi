#ifndef ___LOG_H
#define ___LOG_H

//global loglevel variable
extern int LogLevel;

#define ERRORLOG(a...) void( (LogLevel >= 1) ? syslog_with_tid(LOG_ERR, "DVBAPI-Error: "a) : void() )
#define INFOLOG(a...)  void( (LogLevel >= 2) ? syslog_with_tid(LOG_ERR, "DVBAPI: "a) : void() )
#define DEBUGLOG(a...) void( (LogLevel >= 3) ? syslog_with_tid(LOG_ERR, "DVBAPI: "a) : void() )

#endif // ___LOG_H
