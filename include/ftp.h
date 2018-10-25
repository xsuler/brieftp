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
void sendFile(int connfd,FILE* fptr);
void recvFile(int connfd, FILE *fptr);
void hPasv(State *connst);
void hStor(State *connst, char *msg);
void hQuit(State *connst);
void hAbor(State *connst);
