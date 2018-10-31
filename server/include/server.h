#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <memory.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

typedef struct PState{
  int connfd;
  int id;
  char root[512];
}PState;

enum Cmd {
          USER,
          PASS,
          SYST,
          TYPE,
          PORT,
          PASV,
          RETR,
          STOR,
          MKD,
          CWD,
          PWD,
          RMD,
          LIST,
          DELE,
          RNFR,
          RNTO,
          QUIT,
};

typedef struct State{
  int connfd;
  int vailed;
  int mode; //0 port 1 pasv
  int running;
  int clientPort;
  int id;
  int pasvfd;
  char clientAddr[18];
  char myhost[18];
  char root[512];
  char wd[512];
  char rntemp[512];
  char filename[512];
  char usertemp[512];
  enum Cmd lastcmd;
}State;

typedef int (*Recever)(State*, char *, int);
typedef int (*Init)(State*);
extern Recever pRecv;
int sendMsg(State* connst, char* msg);
void closeConn(State* connst);
int start(int port,char* root);
int createListenSocket(int port,unsigned long saddr);
int createConnSocket(int port,char* saddr);
void strip(char *str);
void strippath(char *str);
