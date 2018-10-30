#include "server.h"

Recever pRecv;
Init pInit;

void strip(char *str) {
  int n = strlen(str);
  char *pt;
  for (pt = str + n - 1; pt >= str; pt--) {
    if ((pt[0] == '\n') || (pt[0] == '\r'))
      pt[0] = 0;
  }
}

int sendMsg(int connfd, char *msg) {
  if (send(connfd, msg, strlen(msg), 0) < 0) {
    perror("Error write()");
    return 1;
  }
  printf("sent %s\n",msg);
  return 0;
}

void closeConn(State *connst) {
  connst->running = 0;
  close(connst->connfd);
  free(connst);
}

int scheduler(PState *connst, void *(*handler)(void *)) {
  pthread_t newthread;

  if (pthread_create(&newthread, NULL, handler, connst)) {
    perror("Error creating thread");
    return 1;
  }
  return 0;
}

void *handler(void *vPtr) {
  PState *pconnst = (PState *)vPtr;
  int connfd = pconnst->connfd;
  int len;
  char buffer[5000];

  State *connst = (State *)malloc(sizeof(State));

  connst->vailed = 0;
  connst->connfd = connfd;
  strcpy(connst->root,pconnst->root);
  strcpy(connst->wd,pconnst->root);
  connst->running = 1;
  connst->id = pconnst->id;
  strcpy(connst->myhost, "127,0,0,1");
  free(pconnst);

  (*pInit)(connst);

  while (connst->running) {

    if ((len = recv(connfd, buffer, 5000, 0)) < 0) {
      break;
    }
    buffer[len] = '\0';

    if ((*pRecv)(connst, buffer, len)) {
      perror("Error pRecv()");
      close(connfd);
    }
  }
  close(connfd);
  return NULL;
}

// need to handle htons
int createConnSocket(int port, char *saddr) {
  struct sockaddr_in addr;
  int connfd;

  if ((connfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("Error socket()");
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(saddr);

  if (connect(connfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("Error connect()");
    return -1;
  }
  return connfd;
}

int createListenSocket(int port, unsigned long saddr) {
  int listenfd;
  struct sockaddr_in addr;
  int flag = 1;

  if ((listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
    perror("Error socket()");
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = port;
  addr.sin_addr.s_addr = saddr;

  if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) ==
      -1) {
    close(listenfd);
    perror("Error setsockopt()");
    return -1;
  }

  if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    close(listenfd);
    perror("Error bind()");
    return -1;
  }

  if ((listen(listenfd, 5) == -1)) {
    close(listenfd);
    perror("Error listen()");
    return -1;
  }
  return listenfd;
}

int start(int port, char *root) {
  srand(time(NULL));
  int listenfd, connfd, count;
  count = 0;
  if ((listenfd = createListenSocket(htons(port), htonl(INADDR_ANY))) < 0) {
    perror("Error listen()");
    return -1;
  }

  while (1) {
    if ((connfd = accept(listenfd, NULL, NULL)) == -1) {
      perror("Error accept()");
      continue;
    }
    PState *st = (PState *)malloc(sizeof(PState));
    st->connfd = connfd;
    strcpy(st->root, root);
    st->id = count;
    scheduler(st, handler);
    count++;
  }

  close(listenfd);
}
