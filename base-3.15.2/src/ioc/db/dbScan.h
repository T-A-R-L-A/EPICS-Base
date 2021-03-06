/*************************************************************************\
* Copyright (c) 2008 UChicago Argonne LLC, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
/* Revision-Id: anj@aps.anl.gov-20140930212203-3q5kofhrtnxjp1pe
 *
 *      Author:         Marty Kraimer
 *      Date:           07-17-91
 */

#ifndef INCdbScanH
#define INCdbScanH

#include <limits.h>

#include "menuScan.h"
#include "shareLib.h"
#include "compilerDependencies.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCAN_PASSIVE        menuScanPassive
#define SCAN_EVENT          menuScanEvent
#define SCAN_IO_EVENT       menuScanI_O_Intr
#define SCAN_1ST_PERIODIC   (menuScanI_O_Intr + 1)

#define MAX_PHASE           SHRT_MAX
#define MIN_PHASE           SHRT_MIN

/*definitions for I/O Interrupt Scanning */
struct ioscan_head;

typedef struct ioscan_head *IOSCANPVT;
typedef struct event_list *EVENTPVT;

typedef void (*io_scan_complete)(void *usr, IOSCANPVT, int prio);

struct dbCommon;

epicsShareFunc long scanInit(void);
epicsShareFunc void scanRun(void);
epicsShareFunc void scanPause(void);
epicsShareFunc void scanShutdown(void);

epicsShareFunc EVENTPVT eventNameToHandle(const char* event);
epicsShareFunc void postEvent(EVENTPVT epvt);
epicsShareFunc void post_event(int event) EPICS_DEPRECATED;
epicsShareFunc void scanAdd(struct dbCommon *);
epicsShareFunc void scanDelete(struct dbCommon *);
epicsShareFunc double scanPeriod(int scan);
epicsShareFunc void scanOnce(struct dbCommon *);
epicsShareFunc int scanOnceSetQueueSize(int size);

/*print periodic lists*/
epicsShareFunc int scanppl(double rate);

/*print event lists*/
epicsShareFunc int scanpel(const char *event_name);

/*print io_event list*/
epicsShareFunc int scanpiol(void);

epicsShareFunc void scanIoInit(IOSCANPVT *ppios);
epicsShareFunc unsigned int scanIoRequest(IOSCANPVT pios);
epicsShareFunc void scanIoSetComplete(IOSCANPVT, io_scan_complete, void *usr);

#ifdef __cplusplus
}
#endif

#endif
