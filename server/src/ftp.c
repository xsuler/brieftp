#include "ftp.h"
#define ChunkSize (4096)
extern Recever pRecv;
extern Init pInit;
Recever pRecv = &onRecv;
Init pInit = &onInit;

// client
// list wildcard and email check
// errno
// report <=3

int isroot(State *connst, char *path) {
  char tmp[512];
  strcpy(tmp, path);
  strippath(tmp);
  int len = strlen(tmp);
  return !strncmp(connst->root, tmp, len);
}

int checkpath(State *connst, char *path) {
  char twd[512];
  chdir(path);
  getcwd(twd, 512);
  chdir(connst->wd);
  return !strncmp(twd, connst->root, strlen(connst->root) - 1);
}

int checkfile(State *connst, char *path) {
  DIR *dir;
  dir = opendir(path);
  int ret = (dir == NULL) && checkpath(connst, path);
  closedir(dir);
  return ret;
}

int checkdir(State *connst, char *path) {
  DIR *dir;
  dir = opendir(path);
  int ret = (dir != NULL) && checkpath(connst, path);
  closedir(dir);
  return ret;
}

int check(char *msg, char *cmd) {
  if (strlen(msg) < strlen(cmd)) {
    return 0;
  }
  return !strncmp(msg, cmd, strlen(cmd));
}

int onInit(State *connst) {
  handShake(connst);
  chdir(connst->wd);
  return 0;
}
/* RNFR, RNTO */
int onRecv(State *connst, char *msg, int len) {
  strip(msg);
  if (strlen(msg) <= 0) {
    hQuit(connst);
    return 0;
  }
  printf("thread %d recv %s\n", connst->id, msg);
  if (check(msg, "USER")) {
    hUser(connst, msg + 5);
    connst->lastcmd = USER;
  } else if (check(msg, "PASS")) {
    hPass(connst, msg + 5);
    connst->lastcmd = PASS;
  } else if (check(msg, "SYST")) {
    hSyst(connst);
    connst->lastcmd = SYST;
  } else if (check(msg, "TYPE")) {
    hType(connst, msg + 5);
    connst->lastcmd = TYPE;
  } else if (check(msg, "PORT")) {
    hPort(connst, msg + 5);
    connst->lastcmd = PORT;
  } else if (check(msg, "PASV")) {
    hPasv(connst);
    connst->lastcmd = PASV;
  } else if (check(msg, "RETR")) {
    hRetr(connst, msg + 5);
    connst->lastcmd = RETR;
  } else if (check(msg, "STOR")) {
    hStor(connst, msg + 5);
    connst->lastcmd = STOR;
  } else if (check(msg, "MKD")) {
    hMkd(connst, msg + 4);
    connst->lastcmd = MKD;
  } else if (check(msg, "CWD")) {
    hCwd(connst, msg + 4);
    connst->lastcmd = CWD;
  } else if (check(msg, "PWD")) {
    hPwd(connst);
    connst->lastcmd = PWD;
  } else if (check(msg, "RMD")) {
    hRmd(connst, msg + 4);
    connst->lastcmd = RMD;
  } else if (check(msg, "LIST")) {
    hList(connst, msg + 5);
    connst->lastcmd = LIST;
  } else if (check(msg, "RNFR")) {
    hRnfr(connst, msg + 5);
    connst->lastcmd = RNFR;
  } else if (check(msg, "RNTO")) {
    hRnto(connst, msg + 5);
    connst->lastcmd = RNTO;
  } else if (check(msg, "QUIT")) {
    hQuit(connst);
    connst->lastcmd = QUIT;
  } else
    sendMsg(connst, "502 command not implemented.\r\n");
  return 0;
}

void handShake(State *connst) { sendMsg(connst, "220 welcome.\r\n"); }

void hUser(State *connst, char *msg) {
  strcpy(connst->usertemp, msg);
  sendMsg(connst, "331 Guest login ok, send your complete e-mail address as "
                  "password.\r\n");
}

void hPass(State *connst, char *msg) {
  if (connst->lastcmd != USER) {
    sendMsg(connst, "503 the previous request was not USER.\r\n");
    return;
  }
  if (!strcmp(connst->usertemp, "anonymous")) {
    connst->vailed = 1;
    sendMsg(connst, "230 welcome to my ftp server.\r\n");
    return;
  }
  sendMsg(connst, "530 username and password are jointly unacceptable.\r\n");
}

void hSyst(State *connst) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  sendMsg(connst, "215 UNIX Type: L8\r\n");
}

void hType(State *connst, char *msg) {
  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }
  if (check(msg, "I") || check(msg, "i")) {
    sendMsg(connst, "200 Type set to I.\r\n");
  } else {
    sendMsg(connst, "504 File Type not supported.\r\n");
  }
}

void hPasv(State *connst) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  int listenfd;
  close(connst->pasvfd);
  int p1, p2, port;
  char smsg[30];
  p1 = rand() % 256;
  p2 = rand() % 256;
  port = p1 * 256 + p2;
  sprintf(smsg, "227 %s,%d,%d\r\n", connst->myhost, p1, p2);
  sendMsg(connst, smsg);
  if ((listenfd = createListenSocket(htons(port), htonl(INADDR_ANY))) < 0) {
    perror("Error listen()");
    return;
  }
  connst->mode = 1;
  connst->pasvfd = listenfd;
}

void hPort(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  close(connst->pasvfd);
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
  sendMsg(connst, "200 PORT command successful.\r\n");
}

void hRetr(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  if (!checkfile(connst, msg)) {
    sendMsg(connst, "550 no such file.\r\n");
    return;
  }

  pthread_t newthread;
  char sendmsg[100];
  sprintf(sendmsg, "150 Opening BINARY mode data connection for %s.\r\n", msg);

  sendMsg(connst, sendmsg);
  strcpy(connst->filename, msg);

  if (pthread_create(&newthread, NULL, sendFile, connst)) {
    sendMsg(connst, "425 Cannot open data connection.\r\n");
    perror("Error creating thread");
    return;
  }
}
void hStor(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  if (!checkpath(connst, msg)) {
    sendMsg(connst, "550 incorrect path.\r\n");
    return;
  }

  pthread_t newthread;
  char sendmsg[100];
  sprintf(sendmsg, "150 Creating upload connection.\r\n");

  sendMsg(connst, sendmsg);

  strcpy(connst->filename, msg);

  if (pthread_create(&newthread, NULL, recvFile, connst)) {
    sendMsg(connst, "425 Cannot open data connection.\r\n");
    perror("Error creating thread");
    return;
  }
}

void hPwd(State *connst) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  char msg[600];
  sprintf(msg, "250 %s\r\n", connst->wd);
  sendMsg(connst, msg);
}

void hRmd(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  if (!checkdir(connst, msg)) {
    sendMsg(connst, "550 no such directory.\r\n");
    return;
  }

  if (rmdir(msg) < 0) {
    sendMsg(connst, "550 the removal failed.\r\n");
    return;
  }
  sendMsg(connst, "250 the directory was successfully removed.\r\n");
}

void getDetail(char *fullpath,char* filename, char *lsinfo) {
  char temp[769];
  struct stat st;
  struct tm *tm;
  stat(fullpath, &st);
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
          filename);
  strcat(lsinfo, temp);
}

void hList(State *connst, char *msg) {
  DIR *dir;
  FILE *fptr;
  struct dirent *file;
  int filefd;
  char path[513];
  char lsinfo[ChunkSize];
  char temp[769];
  int mode;

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  if (!checkpath(connst, msg)) {
    sendMsg(connst, "550 no such file or directory.\r\n");
    return;
  }

  sendMsg(connst, "150 Okay.\r\n");
  if (connst->mode == 0)
    filefd = createConnSocket(connst->clientPort, connst->clientAddr);
  else
    filefd = accept(connst->pasvfd, NULL, NULL);

  if (filefd < 0) {
    sendMsg(connst, "425 no TCP connection was established.\r\n");
    return;
  }

  sprintf(path, "%s/%s", connst->wd, msg);
  dir = opendir(path);
  mode=0;
  if (dir == NULL) {
    fptr = fopen(path, "rb");
    if (fptr == NULL) {
      close(filefd);
      if (connst->mode != 0)
        close(connst->pasvfd);
      sendMsg(connst, "451 cannot open directory from disk.\r\n");
      return;
    }
    fclose(fptr);
    mode=1;
  }
  int is_root = isroot(connst, path);
  while (1) {
    if(mode==1){
      getDetail(path,msg,lsinfo);
    }
    else{
      file = readdir(dir);
      if(file==NULL)break;
      if (is_root && !strcmp(file->d_name, "..")) {
        continue;
      }
      sprintf(temp, "%s/%s", path, file->d_name);
      getDetail(temp,file->d_name,lsinfo);
    }
    if (send(filefd, lsinfo, strlen(lsinfo) * sizeof(char), 0) < 0) {
      sendMsg(connst, "426 data connection broken by the client or by network "
                      "failure.\r\n");
      perror("Error hList()");
      break;
    }
    if(mode==1)break;
  }
  if(mode==0)
    closedir(dir);

  sendMsg(connst, "226 Transfer complete.\r\n");
  close(filefd);
  if (connst->mode != 0)
    close(connst->pasvfd);
}

void hRnto(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  if (connst->lastcmd != RNFR) {
    sendMsg(connst, "503 should be called after RNFR.\r\n");
    return;
  }

  if (!checkpath(connst, msg)) {
    sendMsg(connst, "550 incorrect path.\r\n");
    return;
  }

  if (!rename(connst->rntemp, msg)) {
    sendMsg(connst, "250 the file was renamed successfully.\r\n");
    return;
  }
  // need to use errno
  sendMsg(connst, "550 File unavailable.\r\n");
}
void hRnfr(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  if (!checkpath(connst, msg)) {
    sendMsg(connst, "550 incorrect path.\r\n");
    return;
  }

  if (access(msg, F_OK) == -1) {
    sendMsg(connst, "550 File unavailable.\r\n");
  } else {
    strcpy(connst->rntemp, msg);
    sendMsg(connst, "350 the file exists.\r\n");
  }
}
void hCwd(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  if (!checkdir(connst, msg)) {
    sendMsg(connst, "550 no such directory.\r\n");
    return;
  }

  chdir(msg);
  getcwd(connst->wd, 512);
  sendMsg(connst, "250 Okay.\r\n");
}

void hMkd(State *connst, char *msg) {

  if (!connst->vailed) {
    sendMsg(connst, "503 access denied.\r\n");
    return;
  }

  if (!checkpath(connst, msg)) {
    sendMsg(connst, "550 the creation failed.\r\n");
    return;
  }

  char msgt[600];
  sprintf(msgt, "257 %s\r\n", connst->wd);
  sendMsg(connst, msgt);

  struct stat st = {0};
  if (stat(msg, &st) < 0) {
    mkdir(msg, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    sendMsg(connst, "250  the directory was successfully created.\r\n");
  } else {
    sendMsg(connst, "550 the creation failed.\r\n");
  }
}

void hQuit(State *connst) {
  sendMsg(connst, "221 Bye.\r\n");
  printf("quit: %d\n", connst->id);
  close(connst->pasvfd);
  closeConn(connst);
}

void *sendFile(void *vPtr) {
  State *connst = (State *)vPtr;
  FILE *fptr;
  int filefd;
  char buffer[ChunkSize];
  int n;

  fptr = fopen(connst->filename, "rb");
  if (fptr == NULL) {
    sendMsg(connst, "451 cannot read file from disk.\r\n");
    return NULL;
  }

  if (connst->mode == 0)
    filefd = createConnSocket(connst->clientPort, connst->clientAddr);
  else
    filefd = accept(connst->pasvfd, NULL, NULL);

  if (filefd < 0) {
    sendMsg(connst, "425 no TCP connection was established.\r\n");
    fclose(fptr);
    return NULL;
  }

  while (1) {
    n = fread(buffer, 1, ChunkSize, fptr);
    if (send(filefd, buffer, n * sizeof(char), 0) < 0) {
      sendMsg(connst, "426 data connection broken by the client or by network "
                      "failure.\r\n");
      break;
    }
    if (n <= 0)
      break;
  }

  fclose(fptr);
  sendMsg(connst, "226 Transfer complete.\r\n");
  close(filefd);

  if (connst->mode != 0)
    close(connst->pasvfd);
  return NULL;
}

void *recvFile(void *vPtr) {
  State *connst = (State *)vPtr;
  int filefd;
  char buffer[ChunkSize];
  int n;
  FILE *fptr;

  fptr = fopen(connst->filename, "wb");
  if (fptr == NULL) {
    sendMsg(connst, "451 cannot write file to disk.\r\n");
    return NULL;
  }

  if (connst->mode == 0)
    filefd = createConnSocket(connst->clientPort, connst->clientAddr);
  else
    filefd = accept(connst->pasvfd, NULL, NULL);

  if (filefd < 0) {
    sendMsg(connst, "425 no TCP connection was established.\r\n");
    fclose(fptr);
    return NULL;
  }

  while (1) {
    if ((n = recv(filefd, buffer, ChunkSize * sizeof(char), 0)) < 0) {
      sendMsg(connst, "426 data connection broken by the client or by network "
                      "failure.\r\n");
      break;
    }
    if (n <= 0)
      break;
    if (fwrite(buffer, 1, n, fptr) < 0) {
      sendMsg(connst, "451 cannot write file to disk.\r\n");
      break;
    }
  }

  fclose(fptr);
  sendMsg(connst, "226 Transfer complete.\r\n");
  close(filefd);
  if (connst->mode != 0)
    close(connst->pasvfd);

  return NULL;
}
