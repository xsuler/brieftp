#include "ftp.h"
#define ChunkSize (4096)
extern Recever pRecv;
extern Init pInit;
Recever pRecv = &onRecv;
Init pInit = &onInit;

int check(char *msg, char *cmd) { return !strncmp(msg, cmd, strlen(cmd)); }

int onInit(State *connst) {
  connst->rnflag = 0;
  handShake(connst);
  chdir(connst->wd);
  return 0;
}
/* RNFR, RNTO */
int onRecv(State *connst, char *msg, int len) {
  strip(msg);
  if(strlen(msg)<=0){
    hQuit(connst);
    return 0;
  }
  printf("recv %s\n",msg);
  if (check(msg, "USER"))
    hUser(connst, msg + 5);
  else if (check(msg, "PASS"))
    hPass(connst, msg + 5);
  else if (check(msg, "SYST"))
    hSyst(connst);
  else if (check(msg, "TYPE"))
    hType(connst, msg + 5);
  else if (check(msg, "PORT"))
    hPort(connst, msg + 5);
  else if (check(msg, "PASV"))
    hPasv(connst);
  else if (check(msg, "RETR"))
    hRetr(connst, msg + 5);
  else if (check(msg, "STOR"))
    hStor(connst, msg + 5);
  else if (check(msg, "MKD"))
    hMkd(connst, msg + 4);
  else if (check(msg, "CWD"))
    hCwd(connst, msg + 4);
  else if (check(msg, "PWD"))
    hPwd(connst);
  else if (check(msg, "LIST"))
    hList(connst, msg + 5);
  else if (check(msg, "RNFR"))
    hRnfr(connst, msg + 5);
  else if (check(msg, "RNTO"))
    hRnto(connst, msg + 5);
  else if (check(msg, "RMD"))
    hRmd(connst, msg + 4);
  else if (check(msg, "QUIT"))
    hQuit(connst);
  else
    sendMsg(connst->connfd, "502 \r\n");
  return 0;
}

void handShake(State *connst) {
  int connfd = connst->connfd;
  sendMsg(connfd, "220 welcome\r\n");
}

void hUser(State *connst, char *msg) {
  int connfd = connst->connfd;
  if (check(msg, "anonymous")) {
    connst->vailed = 1;
    sendMsg(connfd, "331 Guest login ok, send your complete e-mail address as "
                    "password.\r\n");
  } else {
    sendMsg(connfd, "504 \r\n");
  }
}

void hPass(State *connst, char *msg) {
  int connfd = connst->connfd;
  sendMsg(connfd, "230 my ftp server\r\n");
}

void hSyst(State *connst) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  int connfd = connst->connfd;
  sendMsg(connfd, "215 UNIX Type: L8\r\n");
}

void hType(State *connst, char *msg) {
  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }
  int connfd = connst->connfd;
  if (check(msg, "I")) {
    sendMsg(connfd, "200 Type set to I.\r\n");
  } else {
    sendMsg(connfd, "504\r\n");
  }
}

void hPasv(State *connst) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  int listenfd;
  int connfd;
  close(connst->pasvfd);
  connfd = connst->connfd;
  int p1, p2, port;
  char smsg[30];
  p1 = rand() % 256;
  p2 = rand() % 256;
  port = p1 * 256 + p2;
  sprintf(smsg, "227 %s,%d,%d\r\n", connst->myhost, p1, p2);
  sendMsg(connfd, smsg);
  if ((listenfd = createListenSocket(htons(port), htonl(INADDR_ANY))) < 0) {
    perror("Error listen()");
    return;
  }
  connst->mode = 1;
  connst->pasvfd = listenfd;
}

void hPort(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  close(connst->pasvfd);
  int connfd = connst->connfd;
  int q;
  int f3, f4;
  int count = 0;
  int len = strlen(msg);
  for (q = 0; q < len; q++) {
    if (msg[q] == ',') {
      if (count <= 2)
        msg[q] = '.';
      else {
        msg[q] = 0;
        if (count == 3) {
          f3 = q;
        } else {
          f4 = q;
        }
      }
      count++;
    }
  }

  connst->clientPort = 256 * atoi(msg + f3 + 1);
  connst->clientPort += atoi(msg + f4 + 1);
  connst->mode = 0;
  strcpy(connst->clientAddr, msg);
  sendMsg(connfd, "200 PORT command successful.\r\n");
}

void hRetr(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  int connfd = connst->connfd;
  pthread_t newthread;
  char sendmsg[100];
  sprintf(sendmsg,
          "150 Opening BINARY mode data connection for %s.\r\n", msg);

  sendMsg(connfd, sendmsg);
  strcpy(connst->filename,msg);

  if (pthread_create(&newthread, NULL,sendFile, connst)) {
    perror("Error creating thread");
    return;
  }

}
void hStor(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  int connfd = connst->connfd;
  pthread_t newthread;
  char sendmsg[100];
  sprintf(sendmsg, "150 Creating upload connection.\r\n");

  sendMsg(connfd, sendmsg);

  strcpy(connst->filename,msg);

  if (pthread_create(&newthread, NULL,recvFile, connst)) {
    perror("Error creating thread");
    return;
  }
}

void hPwd(State *connst) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  char msg[600];
  sprintf(msg, "250 %s\r\n", connst->wd);
  sendMsg(connst->connfd, msg);
}

void hRmd(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  if (rmdir(msg) < 0)
    sendMsg(connst->connfd, "550 \r\n");
  sendMsg(connst->connfd, "250 \r\n");
}

void hList(State *connst, char *msg) {
  DIR *dir;
  struct dirent *file;
  struct stat st;
  struct tm *tm;
  int connfd = connst->connfd;
  int filefd;
  char path[513];
  char pathf[769];
  char temp[282];
  char lsinfo[ChunkSize];

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  sendMsg(connfd, "150 \r\n");
  if (connst->mode == 0)
    filefd = createConnSocket(connst->clientPort, connst->clientAddr);
  else
    filefd = accept(connst->pasvfd, NULL, NULL);

  sprintf(path, "%s/%s", connst->wd, msg);
  dir = opendir(path);
  while ((file = readdir(dir)) != NULL) {
    sprintf(pathf, "%s/%s", path, file->d_name);
    stat(pathf, &st);
    lsinfo[0] = 0;
    strcat(lsinfo, (S_ISDIR(st.st_mode)) ? "d" : "-");
    strcat(lsinfo, (st.st_mode & S_IRUSR) ? "r" : "-");
    strcat(lsinfo, (st.st_mode & S_IWUSR) ? "w" : "-");
    strcat(lsinfo, (st.st_mode & S_IXUSR) ? "x" : "-");
    strcat(lsinfo, (st.st_mode & S_IRGRP) ? "r" : "-");
    strcat(lsinfo, (st.st_mode & S_IWGRP) ? "w" : "-");
    strcat(lsinfo, (st.st_mode & S_IXGRP) ? "x" : "-");
    strcat(lsinfo, (st.st_mode & S_IROTH) ? "r" : "-");
    strcat(lsinfo, (st.st_mode & S_IWOTH) ? "w" : "-");
    strcat(lsinfo, (st.st_mode & S_IXOTH) ? "x" : "-");
    sprintf(temp, " %zu owner group %zu ", st.st_nlink, st.st_size);
    strcat(lsinfo, temp);

    tm = gmtime(&(st.st_mtime));
    switch (tm->tm_mon) {
    case 1:
      strcat(lsinfo, "Jan");
      break;
    case 2:
      strcat(lsinfo, "Feb");
      break;
    case 3:
      strcat(lsinfo, "Mar");
      break;
    case 4:
      strcat(lsinfo, "Apr");
      break;
    case 5:
      strcat(lsinfo, "May");
      break;
    case 6:
      strcat(lsinfo, "Jun");
      break;
    case 7:
      strcat(lsinfo, "Jul");
      break;
    case 8:
      strcat(lsinfo, "Aug");
      break;
    case 9:
      strcat(lsinfo, "Sep");
      break;
    case 10:
      strcat(lsinfo, "Oct");
      break;
    case 11:
      strcat(lsinfo, "Nov");
      break;
    case 12:
      strcat(lsinfo, "Dec");
      break;
    }
    sprintf(temp, " %02d %02d:%02d %s\n", tm->tm_mday, tm->tm_hour, tm->tm_min,
            file->d_name);
    strcat(lsinfo, temp);
    if (send(filefd, lsinfo, strlen(lsinfo) * sizeof(char), 0) < 0) {
      perror("Error hList()");
      break;
    }
  }
  closedir(dir);

  sendMsg(connfd, "226 Transfer complete.\r\n");
  close(filefd);
  if (connst->mode != 0)
    close(connst->pasvfd);
}

void hRnto(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  if (!connst->rnflag) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }
  connst->rnflag = 0;
  if(!rename(connst->rntemp,msg)){
    sendMsg(connst->connfd, "250 \r\n");
    return;
  }
  sendMsg(connst->connfd, "550 \r\n");
}
void hRnfr(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }
  connst->rnflag = 1;
  if (access(msg, F_OK) == -1) {
    sendMsg(connst->connfd, "550 \r\n");
  } else {
    strcpy(connst->rntemp, msg);
    sendMsg(connst->connfd, "350 \r\n");
  }
}
void hCwd(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  // todo: validate

  chdir(msg);
  getcwd(connst->wd, 512);
  sendMsg(connst->connfd, "250 \r\n");
}

void hMkd(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst->connfd, "503 \r\n");
    return;
  }

  sendMsg(connst->connfd, "257 \r\n");
  struct stat st = {0};
  if (stat(msg, &st) < 0) {
    mkdir(msg, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    sendMsg(connst->connfd, "250 \r\n");
  } else {
    sendMsg(connst->connfd, "550 \r\n");
  }
}

void hQuit(State *connst) {
  sendMsg(connst->connfd, "221 Bye.\r\n");
  printf("quit: %d\n", connst->id);
  close(connst->pasvfd);
  closeConn(connst);
}

void* sendFile(void* vPtr) {
  State* connst=(State*)vPtr;
  FILE *fptr;
  int filefd;
  char buffer[ChunkSize];
  int n;
  int connfd;
  connfd=connst->connfd;

  fptr = fopen(connst->filename, "rb");

  if (connst->mode == 0)
    filefd = createConnSocket(connst->clientPort, connst->clientAddr);
  else
    filefd = accept(connst->pasvfd, NULL, NULL);

  while (1) {
    n = fread(buffer, 1, ChunkSize, fptr);
    if (send(filefd, buffer, n * sizeof(char), 0) < 0)
      perror("Error sendFile()");
    if (n <= 0)
      break;
  }

  fclose(fptr);
  sendMsg(connfd, "226 Transfer complete.\r\n");
  close(filefd);

  if (connst->mode != 0)
    close(connst->pasvfd);
  return NULL;
}

void* recvFile(void *vPtr) {
  State* connst=(State*)vPtr;
  int connfd = connst->connfd;
  int filefd;
  char buffer[ChunkSize];
  int n;
  FILE *fptr;

  fptr = fopen(connst->filename, "wb");

  if (connst->mode == 0)
    filefd = createConnSocket(connst->clientPort, connst->clientAddr);
  else
    filefd = accept(connst->pasvfd, NULL, NULL);

  while (1) {
    if ((n = recv(filefd, buffer, ChunkSize * sizeof(char), 0)) < 0) {
      perror("Error recv()");
      break;
    }
    if (n <= 0)
      break;
    if (fwrite(buffer, 1, n, fptr) < 0) {
      perror("Error sendFile()");
      return NULL;
    }
  }

  fclose(fptr);
  sendMsg(connfd, "226 Transfer complete.\r\n");
  close(filefd);
  if (connst->mode != 0)
    close(connst->pasvfd);
  return NULL;
}
