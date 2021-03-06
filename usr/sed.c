// Simple sed.
// Only supports s and d command.
// Not checked regexp
// (example)
//  * sed 1,5d
//   -> delete line 1 to 5
//  * sed 2s/abc/pqr/g
//   -> substitute pqr for abc in line 2
//  * sed 3iabc
//   -> insert abc before line 3
//  * sed 3apqr
//   -> insert pqr after line 3

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

char buf[1024];
char *extractline(char *option, int *start, int *end);

void sed_delete(int fd, int start, int end);
void sed_substitute(int fd, int start, int end, char *option);
void sed_insert(int fd, int start, int end, char *txt);
void sed_append(int fd, int start, int end, char *txt);

void sed(char *option, int fd){
  int start, end;

  option = extractline(option, &start, &end);

  switch(*option){
  case 'd':
    sed_delete(fd, start, end);
    return;
  case 's':
    option++;
    sed_substitute(fd, start, end, option);
    return;
  case 'a':
    option++;
    sed_append(fd, start, end, option);
    return;
  case 'i':
    option++;
    sed_insert(fd, start, end, option);
    return;
  default:
    printf("sed: wrong command\n");
    return;
  }
}

int main(int argc, char *argv[]){
  int fd, i;
  char *option;

  if(argc <= 1){
    fprintf(stderr, "usage: sed [OPTION] [input]\n");
    fprintf(stderr, "OPTION: [line][command]\n");
    fprintf(stderr, "command: d(delete), s(substitute), a(append), i(insert)\n");
    exit(1);
  }
  option = argv[1];

  if(argc <= 2){
    sed(option, 0);
    exit(0);
  }

  for(i = 2; i < argc; i++){
    if((fd = open(argv[i], 0)) < 0){
      fprintf(stderr, "sed: cannot open %s\n", argv[i]);
      exit(1);
    }
    sed(option, fd);
    close(fd);
  }

  exit(0);
}

// extract line option
char *extractline(char *option, int *start, int *end){
  *start = 0;
  *end   = -1;

  // line optioned
  while(*option >= '0' && *option <= '9'){
    *start += (*start)*10 + ((*option) - '0');
    option++;
  }
  if(*option != ','){
    if(*start == 0){
      *start = 1;
      return option;
    }
    *end = *start;
    return option;
  }
  option++;
  *end = 0;
  while(*option >= '0' && *option <= '9'){
    *end += (*end)*10 + ((*option) - '0');
    option++;
  }
  return option;
}

// line is in range
int isrange(int start, int end, int line){
  return start <= line && (line <= end || end == -1);
}


// delete line
void sed_delete(int fd, int start, int end){
  int n, m, l;
  char *p, *q;

  m = 0;
  l = 0;
  while((n = read(fd, buf+m, sizeof(buf)-m-1)) >0 ){
    buf[m+n]=0;
    m += n;
    p = buf;
    while((q = strchr(p, '\n')) != 0){
      l++;
      *q = 0;

      if(!isrange(start, end, l)){
        *q = '\n';
        write(1, p, q+1-p);
      }
      p = q+1;
    }
    if(p == buf)
      m=0;
    if(m > 0){
      m -= p - buf;
      memmove(buf, p, m);
      memset(buf+m, '\0', sizeof(buf)-m);
    }
  }
}

int parse_regexp_sub(char *re, char *from, char *to, int *g){
  char *p, *q;

  if(*re != '/') return -1;
  re++;

  p = re;
  if((q = strchr(p, '/')) == 0)
    return -1;

  strcpy(from, p);
  from[q-p] = '\0';

  p = q+1;
  if((q = strchr(p, '/')) == 0)
    return -1;

  strcpy(to, p);
  to[q-p] = '\0';

  if(*q != '/')
    return -1;

  if(*(q+1) == 'g')
    *g = 1;
  else
    *g = 0;

  return 0;
}

void outputreplace(char *text, char *s, char *e, char *to){
  int i, j;
  for(i=0; *(to+i) != '\0'; ++i)
    ;
  for(j=0; *(e+j) != '\n'; ++j)
    ;
  write(1, text, s-text);
  write(1, to, i);
  write(1, e, j+1);
}

char *replacebegin(char *re, char *text);
char *replacestar(int c, char *re, char *text);

int replace(char *from, char *to, char *text){
  char *bak,*s,*e;
  bak = text;
  if(from[0] == '^'){
    s = text;
    e = replacebegin(from+1, text);
    outputreplace(bak, s, e, to);
    return 1;
  }
  do{  // must look at empty string
    s = text;
    if((e = replacebegin(from, text)) != 0){
      outputreplace(bak, s, e, to);
      return 1;
    }
  }while(*text++ != '\0');
  return 0;
}

// replacebegin: search for re at beginning of text
char *replacebegin(char *re, char *text){
  if(re[0] == '\0'){
    return text;
  }
  if(re[1] == '*'){
    return replacestar(re[0], re+2, text);
  }
  if(re[0] == '$' && re[1] == '\0'){
    if(*text == '\0')
      return text;
    else
      return 0;
  }
  if(*text!='\0' && (re[0]=='.' || re[0]==*text)){
    return replacebegin(re+1, text+1);
  }
  return 0;
}

// replacestar: search for c*re at beginning of text
char *replacestar(int c, char *re, char *text){
  char *tmp;
  do{  // a * replacees zero or more instances
    if(tmp = replacebegin(re, text))
      return tmp;
  }while(*text!='\0' && (*text++==c || c=='.'));
  return 0;
}

// substitute line
void sed_substitute(int fd, int start, int end, char *re){
  int n, m, l, g;
  char *p, *q;
  char from[128], to[128];

  if(parse_regexp_sub(re, from, to, &g) < 0){
    fprintf(stderr, "sed: unknown regexp\n");
    exit(1);
  }

  m = 0;
  l = 0;
  while((n = read(fd, buf+m, sizeof(buf)-m-1)) >0 ){
    buf[m+n]=0;
    m += n;
    p = buf;
    while((q = strchr(p, '\n')) != 0){
      l++;
      *q = 0;

      if(!isrange(start, end, l)){
        *q = '\n';
        write(1, p, q+1-p);
      }else{
        *q = '\n';
        if(replace(from, to, p)){
          // write in replace
        }else{
          write(1, p, q+1-p);
        }
      }
      p = q+1;
    }
    if(p == buf)
      m=0;
    if(m > 0){
      m -= p - buf;
      memmove(buf, p, m);
      memset(buf+m, '\0', sizeof(buf)-m);
    }
  }
}

// append, insert
void sed_insert_append(int flg, int fd, int start, int end, char *txt);
void sed_insert(int fd, int start, int end, char *txt){
  sed_insert_append(0, fd, start, end, txt);
}
void sed_append(int fd, int start, int end, char *txt){
  sed_insert_append(1, fd, start, end, txt);
}
// flg
//  0 -> insert
//  1 -> append
void sed_insert_append(int flg, int fd, int start, int end, char *txt){
  int i, n, m, l, g;
  char *p, *q;

  for(i=0; *(txt+i) != '\0'; ++i)
    ;
  *(txt+i) = '\n';
  i++;

  m = 0;
  l = 0;
  while((n = read(fd, buf+m, sizeof(buf)-m-1)) >0 ){
    buf[m+n]=0;
    m += n;
    p = buf;
    while((q = strchr(p, '\n')) != 0){
      l++;
      *q = 0;

      if(!isrange(start, end, l)){
        *q = '\n';
        write(1, p, q+1-p);
      }else{
        *q = '\n';
        if(flg == 0)
          write(1, txt, i);
        write(1, p, q+1-p);
        if(flg == 1)
          write(1, txt, i);
      }
      p = q+1;
    }
    if(p == buf)
      m=0;
    if(m > 0){
      m -= p - buf;
      memmove(buf, p, m);
      memset(buf+m, '\0', sizeof(buf)-m);
    }
  }
}
