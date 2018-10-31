#include "server.h"

int onRecv(State* connst, char *msg, int len);
int onInit(State* connst);
void handShake(State* connst);
void hUser(State* connst,char *msg);
void hPass(State* connst,char *msg);
void hSyst(State* connst);
void hType(State* connst,char* msg);
void hPort(State* connst,char* msg);
void hRetr(State *connst, char *msg);
void* sendFile(void* vPtr);
void* recvFile(void* vPtr);
void hPasv(State *connst);
void hStor(State *connst, char *msg);
void hQuit(State *connst);
void hAbor(State *connst);
void hMkd(State *connst,char* msg);
void hCwd(State *connst,char* msg);
void hPwd(State *connst);
void hRmd(State *connst, char *msg);
void hDele(State *connst, char *msg);
void hList(State *connst, char *msg);
void hRnto(State *connst, char *msg);
void hRnfr(State *connst, char *msg);
