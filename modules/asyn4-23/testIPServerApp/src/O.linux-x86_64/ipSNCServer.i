# 1 "../ipSNCServer.st"
# 1 "<built-in>"
# 1 "<command-line>"
# 1 "/usr/include/stdc-predef.h" 1 3 4
# 1 "<command-line>" 2
# 1 "../ipSNCServer.st"
program ipSNCServer("P=testIPServer:=, PORT=P5000")

option +r;

%%#include <string.h>
%%#include <epicsString.h>
%%#include <epicsEvent.h>
%%#include <asynDriver.h>
%%#include <asynOctet.h>
%%#include <asynOctetSyncIO.h>



string input; assign input to "{P}stringInput";
string output; assign output to "{P}stringOutput";
int connected; assign connected to "{P}connected";

char *listenerPortName;
char *IOPortName;
int readStatus;
int writeStatus;
char *pasynUser;
char *registrarPvt;
char *eventId;


%% static void initialize(SS_ID ssId, struct UserVar *pVar);
%% static int readSocket(SS_ID ssId, struct UserVar *pVar);
%% static int writeSocket(SS_ID ssId, struct UserVar *pVar);

ss ipSNCServer
{
    state init {
        when() {
            listenerPortName = macValueGet("PORT");
            %%initialize(ssId, pVar);
            connected = 0;
        } state waitConnect
    }

    state waitConnect {
        when() {
            pvPut(connected);
            %%epicsEventWait( (epicsEventId)pVar->eventId);
            pvPut(connected);
        } state processCommands
    }

    state processCommands {
        when(connected) {
            %%pVar->readStatus = readSocket(ssId, pVar);
            if (readStatus == 0) {
                pvPut(input);
                strcpy(output, "OK");
                %%pVar->writeStatus = writeSocket(ssId, pVar);
                if (writeStatus == 0) {
                    pvPut(output);
                }
            }
        } state processCommands

        when(!connected) {
        } state waitConnect

    }
}

%{
static void connectionCallback(void *drvPvt, asynUser *pasynUserIn, char *portName, size_t len, int eomReason)
{
    struct UserVar *pVar = (struct UserVar *)drvPvt;
    asynUser *pasynUser;
    asynStatus status;

    pVar->IOPortName = epicsStrDup(portName);
    status = pasynOctetSyncIO->connect(portName, 0, &pasynUser, NULL);
    pVar->pasynUser = (char *)pasynUser;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "ipSNCServer: connectionCallback, portName=%s\n", portName);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:connectionCallback: unable to connect to port %s\n",
                  portName);
        return;
    }
    status = pasynOctetSyncIO->setInputEos(pasynUser, "\r\n", 2);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:connectionCallback: unable to set input EOS on %s: %s\n",
                  portName, pasynUser->errorMessage);
        return;
    }
    status = pasynOctetSyncIO->setOutputEos(pasynUser, "\r\n", 2);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:connectionCallback: unable to set output EOS on %s: %s\n",
                  portName, pasynUser->errorMessage);
        return;
    }

    pVar->connected = 1;
    epicsEventSignal( (epicsEventId)pVar->eventId);
}

static void initialize(SS_ID ssId, struct UserVar *pVar)
{
    int addr=0;
    asynInterface *pasynInterface;
    asynUser *pasynUser;
    asynOctet *pasynOctet;
    void *registrarPvt;
    int status;

    pVar->eventId = (char *)epicsEventCreate(epicsEventEmpty);
    pasynUser = pasynManager->createAsynUser(0,0);
    pVar->pasynUser = (char *)pasynUser;
    pasynUser->userPvt = pVar;
    status = pasynManager->connectDevice(pasynUser, pVar->listenerPortName, addr);
    if(status!=asynSuccess) {
        printf("can't connect to port %s: %s\n", pVar->listenerPortName, pasynUser->errorMessage);
        return;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s driver not supported\n",asynOctetType);
        return;
    }
    pasynOctet = (asynOctet *)pasynInterface->pinterface;
    status = pasynOctet->registerInterruptUser(
                 pasynInterface->drvPvt, pasynUser,
                 connectionCallback,pVar, &registrarPvt);
    pVar->registrarPvt = registrarPvt;
    if(status!=asynSuccess) {
        printf("ipSNCServer devAsynOctet registerInterruptUser %s\n",
               pasynUser->errorMessage);
    }
}

static int readSocket(SS_ID ssId, struct UserVar *pVar)
{
    char buffer[80];
    size_t nread;
    int eomReason;
    asynUser *pasynUser = (asynUser *)pVar->pasynUser;
    asynStatus status;

    status = pasynOctetSyncIO->read(pasynUser, buffer, 80,
                                    -1.0, &nread, &eomReason);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:readSocket: read error on: %s: %s\n",
                  pVar->IOPortName, pasynUser->errorMessage);
        pVar->connected = 0;
        strcpy(pVar->input, "");
    }
    else {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                  "ipSNCServer:readSocket: %s read %s\n",
                   pVar->IOPortName, buffer);
        strcpy(pVar->input, buffer);
    }
    return(status);
}

static int writeSocket(SS_ID ssId, struct UserVar *pVar)
{
    size_t nwrite;
    asynUser *pasynUser = (asynUser *)pVar->pasynUser;
    asynStatus status;

    status = pasynOctetSyncIO->write(pasynUser, pVar->output, strlen(pVar->output),
                                     0.0, &nwrite);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "ipSNCServer:writeSocket: write error on: %s: %s\n",
                  pVar->IOPortName, pasynUser->errorMessage);
        pVar->connected = 0;
    }
    else {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                   "ipSNCServer:writeSocket: %s write %s\n",
                   pVar->IOPortName, pVar->output);
    }
    return(status);
}

}%
