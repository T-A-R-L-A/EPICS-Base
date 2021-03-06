/*************************************************************************\
* Copyright (c) 2008 UChicago Argonne LLC, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* Copyright (c) 2013 ITER Organization.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/
/* callback.c */

/* general purpose callback tasks		*/
/*
 *      Original Author:        Marty Kraimer
 *      Date:   	        07-18-91
*/

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cantProceed.h"
#include "dbDefs.h"
#include "epicsAtomic.h"
#include "epicsEvent.h"
#include "epicsExit.h"
#include "epicsInterrupt.h"
#include "epicsRingPointer.h"
#include "epicsString.h"
#include "epicsThread.h"
#include "epicsTimer.h"
#include "errlog.h"
#include "errMdef.h"
#include "taskwd.h"

#define epicsExportSharedSymbols
#include "callback.h"
#include "dbAccessDefs.h"
#include "dbAddr.h"
#include "dbBase.h"
#include "dbCommon.h"
#include "dbFldTypes.h"
#include "dbLock.h"
#include "dbStaticLib.h"
#include "epicsExport.h"
#include "link.h"
#include "recSup.h"


static int callbackQueueSize = 2000;

typedef struct cbQueueSet {
    epicsEventId semWakeUp;
    epicsRingPointerId queue;
    int queueOverflow;
    int threadsConfigured;
    int threadsRunning;
} cbQueueSet;

static cbQueueSet callbackQueue[NUM_CALLBACK_PRIORITIES];

int callbackThreadsDefault = 1;
/* Don't know what a reasonable default is (yet).
 * For the time being: parallel means 2 if not explicitly specified */
epicsShareDef int callbackParallelThreadsDefault = 2;
epicsExportAddress(int,callbackParallelThreadsDefault);

/* Timer for Delayed Requests */
static epicsTimerQueueId timerQueue;

/* Shutdown handling */
enum ctl {ctlInit, ctlRun, ctlPause, ctlExit};
static volatile enum ctl cbCtl;
static epicsEventId startStopEvent;
static void *exitCallback;

/* Static data */
static char *threadNamePrefix[NUM_CALLBACK_PRIORITIES] = {
    "cbLow", "cbMedium", "cbHigh"
};
static unsigned int threadPriority[NUM_CALLBACK_PRIORITIES] = {
    epicsThreadPriorityScanLow - 1,
    epicsThreadPriorityScanLow + 4,
    epicsThreadPriorityScanHigh + 1
};
static int priorityValue[NUM_CALLBACK_PRIORITIES] = {0, 1, 2};


int callbackSetQueueSize(int size)
{
    if (startStopEvent) {
        errlogPrintf("Callback system already initialized\n");
        return -1;
    }
    callbackQueueSize = size;
    return 0;
}

int callbackParallelThreads(int count, const char *prio)
{
    if (startStopEvent) {
        errlogPrintf("Callback system already initialized\n");
        return -1;
    }

    if (count < 0)
        count = epicsThreadGetCPUs() + count;
    else if (count == 0)
        count = callbackParallelThreadsDefault;
    if (count < 1) count = 1;

    if (!prio || strcmp(prio, "") == 0 || strcmp(prio, "*") == 0) {
        int i;

        for (i = 0; i < NUM_CALLBACK_PRIORITIES; i++) {
            callbackQueue[i].threadsConfigured = count;
        }
    }
    else {
        dbMenu *pdbMenu;

        if (!pdbbase) {
            errlogPrintf("callbackParallelThreads: pdbbase not set\n");
            return -1;
        }
        /* Find prio in menuPriority */
        pdbMenu = dbFindMenu(pdbbase, "menuPriority");
        if (pdbMenu) {
            int i, gotMatch = 0;

            for (i = 0; i < pdbMenu->nChoice; i++) {
                gotMatch = (epicsStrCaseCmp(prio, pdbMenu->papChoiceValue[i])==0) ? TRUE : FALSE;
                if (gotMatch)
                    break;
            }
            if (gotMatch) {
                callbackQueue[i].threadsConfigured = count;
                return 0;
            }
            else {
                errlogPrintf("Unknown priority \"%s\"\n", prio);
                return -1;
            }
        }
    }
    return 0;
}

static void callbackTask(void *arg)
{
    cbQueueSet *mySet = &callbackQueue[*(int*)arg];

    taskwdInsert(0, NULL, NULL);
    epicsEventSignal(startStopEvent);

    while(TRUE) {
        void *ptr;
        if (epicsRingPointerIsEmpty(mySet->queue))
            epicsEventMustWait(mySet->semWakeUp);

        while ((ptr = epicsRingPointerPop(mySet->queue))) {
            CALLBACK *pcallback = (CALLBACK *)ptr;
            if(!epicsRingPointerIsEmpty(mySet->queue))
                epicsEventMustTrigger(mySet->semWakeUp);
            if (ptr == &exitCallback) goto shutdown;
            mySet->queueOverflow = FALSE;
            (*pcallback->callback)(pcallback);
        }
    }

shutdown:
    mySet->threadsRunning--;
    taskwdRemove(0);
    epicsEventSignal(startStopEvent);
}

void callbackShutdown(void)
{
    int i;

    if (cbCtl == ctlExit) return;
    cbCtl = ctlExit;

    /* sequential shutdown of workers */
    for (i = 0; i < NUM_CALLBACK_PRIORITIES; i++) {
        while (callbackQueue[i].threadsRunning) {
            if(epicsRingPointerPush(callbackQueue[i].queue, &exitCallback)) {
                epicsEventSignal(callbackQueue[i].semWakeUp);
                epicsEventWait(startStopEvent);
            } else {
                epicsThreadSleep(0.05);
            }
        }
        assert(callbackQueue[i].threadsRunning==0);
        epicsEventDestroy(callbackQueue[i].semWakeUp);
        epicsRingPointerDelete(callbackQueue[i].queue);
    }
    epicsTimerQueueRelease(timerQueue);
    epicsEventDestroy(startStopEvent);
    startStopEvent = NULL;
    memset(callbackQueue, 0, sizeof(callbackQueue));
}

void callbackInit(void)
{
    int i;
    int j;
    char threadName[32];

    if (startStopEvent)
        return;

    startStopEvent = epicsEventMustCreate(epicsEventEmpty);
    cbCtl = ctlRun;
    timerQueue = epicsTimerQueueAllocate(0, epicsThreadPriorityScanHigh);

    for (i = 0; i < NUM_CALLBACK_PRIORITIES; i++) {
        epicsThreadId tid;

        callbackQueue[i].semWakeUp = epicsEventMustCreate(epicsEventEmpty);
        callbackQueue[i].queue = epicsRingPointerLockedCreate(callbackQueueSize);
        if (callbackQueue[i].queue == 0)
            cantProceed("epicsRingPointerLockedCreate failed for %s\n",
                threadNamePrefix[i]);
        callbackQueue[i].queueOverflow = FALSE;
        if (callbackQueue[i].threadsConfigured == 0)
            callbackQueue[i].threadsConfigured = callbackThreadsDefault;

        for (j = 0; j < callbackQueue[i].threadsConfigured; j++) {
            if (callbackQueue[i].threadsConfigured > 1 )
                sprintf(threadName, "%s-%d", threadNamePrefix[i], j);
            else
                strcpy(threadName, threadNamePrefix[i]);
            tid = epicsThreadCreate(threadName, threadPriority[i],
                epicsThreadGetStackSize(epicsThreadStackBig),
                (EPICSTHREADFUNC)callbackTask, &priorityValue[i]);
            if (tid == 0) {
                cantProceed("Failed to spawn callback thread %s\n", threadName);
            } else {
                epicsEventWait(startStopEvent);
                callbackQueue[i].threadsRunning++;
            }
        }
    }
}

/* This routine can be called from interrupt context */
int callbackRequest(CALLBACK *pcallback)
{
    int priority;
    int pushOK;
    cbQueueSet *mySet;

    if (!pcallback) {
        epicsInterruptContextMessage("callbackRequest: pcallback was NULL\n");
        return S_db_notInit;
    }
    priority = pcallback->priority;
    if (priority < 0 || priority >= NUM_CALLBACK_PRIORITIES) {
        epicsInterruptContextMessage("callbackRequest: Bad priority\n");
        return S_db_badChoice;
    }
    mySet = &callbackQueue[priority];
    if (mySet->queueOverflow) return S_db_bufFull;

    pushOK = epicsRingPointerPush(mySet->queue, pcallback);

    if (!pushOK) {
        char msg[48] = "callbackRequest: ";

        strcat(msg, threadNamePrefix[priority]);
        strcat(msg, " ring buffer full\n");
        epicsInterruptContextMessage(msg);
        mySet->queueOverflow = TRUE;
        return S_db_bufFull;
    }
    epicsEventSignal(mySet->semWakeUp);
    return 0;
}

static void ProcessCallback(CALLBACK *pcallback)
{
    dbCommon *pRec;

    callbackGetUser(pRec, pcallback);
    if (!pRec) return;
    dbScanLock(pRec);
    (*pRec->rset->process)(pRec);
    dbScanUnlock(pRec);
}

void callbackSetProcess(CALLBACK *pcallback, int Priority, void *pRec)
{
    callbackSetCallback(ProcessCallback, pcallback);
    callbackSetPriority(Priority, pcallback);
    callbackSetUser(pRec, pcallback);
}

int  callbackRequestProcessCallback(CALLBACK *pcallback,
    int Priority, void *pRec)
{
    callbackSetProcess(pcallback, Priority, pRec);
    return callbackRequest(pcallback);
}

static void notify(void *pPrivate)
{
    CALLBACK *pcallback = (CALLBACK *)pPrivate;
    callbackRequest(pcallback);
}

void callbackRequestDelayed(CALLBACK *pcallback, double seconds)
{
    epicsTimerId timer = (epicsTimerId)pcallback->timer;

    if (timer == 0) {
        timer = epicsTimerQueueCreateTimer(timerQueue, notify, pcallback);
        pcallback->timer = timer;
    }
    epicsTimerStartDelay(timer, seconds);
}

void callbackCancelDelayed(CALLBACK *pcallback)
{
    epicsTimerId timer = (epicsTimerId)pcallback->timer;

    if (timer != 0) {
        epicsTimerCancel(timer);
    }
}

void callbackRequestProcessCallbackDelayed(CALLBACK *pcallback,
    int Priority, void *pRec, double seconds)
{
    callbackSetProcess(pcallback, Priority, pRec);
    callbackRequestDelayed(pcallback, seconds);
}
