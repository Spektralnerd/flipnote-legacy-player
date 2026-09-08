#include <stddef.h>
typedef struct F FILE; typedef struct D DIR; typedef unsigned long pthread_t;
struct dirent { unsigned long long a; long long b; unsigned short c; unsigned char d; char n[256]; };
void* malloc(size_t n){return 0;} void free(void*p){} void* memset(void*a,int b,size_t c){return a;}
void* memcpy(void*a,const void*b,size_t c){return a;} size_t strlen(const char*a){return 0;}
int strcmp(const char*a,const char*b){return 0;} char* strncpy(char*a,const char*b,size_t c){return a;}
FILE* fopen(const char*a,const char*b){return 0;} size_t fread(void*a,size_t b,size_t c,FILE*d){return 0;}
int fseek(FILE*a,long b,int c){return 0;} long ftell(FILE*a){return 0;} int fclose(FILE*a){return 0;}
DIR* opendir(const char*a){return 0;} struct dirent* readdir(DIR*a){return 0;} int closedir(DIR*a){return 0;}
void qsort(void*a,size_t b,size_t c,int(*d)(const void*,const void*)){} int usleep(unsigned int a){return 0;}
struct timeval { long tv_sec; long tv_usec; }; int gettimeofday(struct timeval*a,void*b){return 0;}
int pthread_create(pthread_t*a,const void*b,void*(*c)(void*),void*d){return 0;} int pthread_join(pthread_t a,void**b){return 0;}
