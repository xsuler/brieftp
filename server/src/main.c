#include"server.h"

int main(int argc, char **argv) {
  int p;
  int port=21;
  char root[512];
  strcpy(root,"/tmp/");
  int rtlen;
  for (p=1;p<argc;p++){
    if(!strcmp(argv[p],"-port")){
      port=atoi(argv[p+1]);
      p++;
      continue;
    }
    if(!strcmp(argv[p],"-root")){
      strcpy(root,argv[p+1]);
      rtlen=strlen(root);
      if(root[rtlen-1]!='/'){
        root[rtlen]='/';
        root[rtlen+1]=0;
      }
      p++;
      continue;
    }
  }

  printf("%s,%d",root,port);
  if(!start(port,root))
    printf("Error start server: %s(%d)\n", strerror(errno), errno);
  return 0;
}
