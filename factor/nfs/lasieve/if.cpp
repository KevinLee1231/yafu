

#include "if.h"
#include <time.h> 
#include <unistd.h> 
#ifndef _WIN64 
#include <sys/times.h> 
#endif
#include <stdio.h> 
#include <stdlib.h> 
#include <stdarg.h> 
#include <string.h> 
#include <gmp.h> 
#include <limits.h> 
#include "siever-config.h"
#include "lasieve_bail.h"

#include "lasieve_ns.h"

namespace lasieve_ns {
int verbose= 0;
static unsigned int used_cols,ncol= 80;

/*:2*//*3:*/

void*xmalloc(size_t size)
{
char*x;
if(size==0)return NULL;
if((x= (char*)malloc(size))==NULL)complain("xmalloc: %m\n");
return x;
}

/*:3*//*4:*/

#if defined( _MSC_VER ) && defined( _WIN64 )
#error _MSC_VER
void*xvalloc(size_t size)
{
char*x;
static long g_pagesize= 0;
if(!g_pagesize)
{
SYSTEM_INFO system_info;
GetSystemInfo(&system_info);
g_pagesize= system_info.dwPageSize;
}
if(size==0)return NULL;
if((x= _aligned_malloc(size,g_pagesize))==NULL)complain("xvalloc: %m\n");
return x;
}

#elif defined (_WIN64)

void*xvalloc(size_t size)
{
char*x;
if(size==0)return NULL;
if((x= _aligned_malloc(size,4096))==NULL)complain("xvalloc: %m\n");
return x;
}

#else

void*xvalloc(size_t size)
{
char*x;
if(size==0)return NULL;
if((x= (char*)valloc(size))==NULL)complain("xvalloc: %m\n");
return x;
}
#endif

/*:4*//*5:*/

void*
xcalloc(size_t n,size_t s)
{
void*p;
if(n==0||s==0)return NULL;
if((p= calloc(n,s))==NULL)complain("calloc: %m\n");
return p;
}

/*:5*//*6:*/

void*xrealloc(void*x,size_t size)
{
char*y;
if(size==0){
if(x!=NULL)free(x);
return NULL;
}
if((y= (char*)realloc(x,size))==NULL&&size!=0)complain("xrealloc: %m\n");
return y;
}

/*:6*//*7:*/

FILE*logfile= NULL;



#define MAXMSGLEN 20000
void complain(char*fmt,...)
{
char msg[MAXMSGLEN];

va_list arglist;
va_start(arglist,fmt);

int qlen= vsnprintf(msg,MAXMSGLEN,fmt,arglist);

if(qlen<0||qlen> MAXMSGLEN){
fprintf(stderr,"ERROR: vsnprintf call failed during complain\n");
}else{
#if defined (__APPLE__) || defined (_WIN64)
int lenfmt= strlen(fmt);
if(lenfmt> 4&&fmt[lenfmt-3]=='%'&&fmt[lenfmt-2]=='m'&&fmt[lenfmt-1]=='\n'){

msg[qlen-2]= '\0';

snprintf(msg,MAXMSGLEN,"%s%s\n",msg,strerror(errno));
}
#endif
}


fprintf(stderr,"%s",msg);

va_end(arglist);

if(logfile!=NULL){
fprintf(logfile,"%s",msg);
}
#ifdef HAVE_BOINC
boinc_finish(1);
#else
 lasieve_bail(1);
#endif
}

/*:7*//*8:*/

void Schlendrian(char*fmt,...)
{
va_list arglist,log_args;
va_start(arglist,fmt);
if(logfile!=NULL){
va_copy(log_args,arglist);
vfprintf(logfile,fmt,log_args);
va_end(log_args);
}
vfprintf(stderr,fmt,arglist);
va_end(arglist);
#ifdef HAVE_BOINC
boinc_finish(1);
#else
 lasieve_bail(1);
#endif
}

/*:8*//*9:*/

#define NEW_NUMBER -0x10000
void logbook(int l, char* fmt, ...)
{
    if (l == NEW_NUMBER) {
        used_cols = 0;
        return;
    }
    if (l < verbose) {
        va_list arglist;
        char* output_str;
        unsigned int sl;
        va_start(arglist, fmt);
        if (vasprintf(&output_str, fmt, arglist) < 0) {
            va_end(arglist);
            complain("logbook: cannot format message\n");
        }
        va_end(arglist);
        sl = strlen(output_str);
        if (used_cols + sl > ncol) {
            fprintf(stderr, "\n");
            if (logfile != NULL)fprintf(logfile, "\n");
            used_cols = 0;
        }
        fputs(output_str, stderr);
        if (logfile != NULL)fputs(output_str, logfile);
        if (output_str[sl - 1] == '\n')used_cols = 0;
        else used_cols += sl;
        free(output_str);
    }
}

/*:9*//*10:*/

int
errprintf(char*fmt,...)
{
va_list arglist,log_args;
int res;

va_start(arglist,fmt);
if(logfile!=NULL){
va_copy(log_args,arglist);
vfprintf(logfile,fmt,log_args);
va_end(log_args);
}
res= vfprintf(stderr,fmt,arglist);
va_end(arglist);
return res;
}

/*:10*//*11:*/

void adjust_bufsize(void**buf,size_t*alloc,size_t req,
size_t incr,size_t item_size)
{
if(req> *alloc){
size_t new_alloc;
new_alloc= *alloc+incr*((req+incr-1-*alloc)/incr);
if(*alloc> 0)*buf= xrealloc(*buf,new_alloc*item_size);
else*buf= xmalloc(new_alloc*item_size);
*alloc= new_alloc;
}
}

/*:11*//*12:*/

int yn_query(char*fmt,...)
{
va_list arglist;
char answer[10];
char*question;
int result;

va_start(arglist,fmt);
if(vasprintf(&question,fmt,arglist)<0){
va_end(arglist);
complain("yn_query: cannot format question\n");
}
va_end(arglist);
if(logfile!=NULL)fputs(question,logfile);
fputs(question,stderr);
if(!isatty(STDIN_FILENO)||!isatty(STDERR_FILENO)){
free(question);
return 0;
}

fflush(stderr);
while(scanf("%9s",answer)!=1||
(strcasecmp(answer,"yes")!=0&&strcasecmp(answer,"no")!=0)){
fprintf(stderr,"Please answer yes or no!\n");
fputs(question,stderr);
}
result= strcasecmp(answer,"yes")==0?1:0;
free(question);
return result;
}


/*:12*//*13:*/

ssize_t
skip_blanks_comments(char**iline,size_t*iline_alloc,FILE*ifi)
{
while(getline(iline,iline_alloc,ifi)> 0){
if(**iline!='#'&&strspn(*iline,"\n\t ")<strlen(*iline))
return 1;
}
return 0;
}

/*:13*//*14:*/

#ifdef BIGENDIAN

static u32_t
bswap_32(u32_t x)
{
return((x&0x000000ffUL)<<24)|((x&0x0000ff00UL)<<8)|
((x&0x00ff0000UL)>>8)|((x&0xff000000UL)>>24);
}

/*:15*//*16:*/

static u64_t
bswap_64(u64_t x)
{
return((x&0xffULL)<<56)|((x&0xff00ULL)<<40)|((x&0xff0000ULL)<<24)|
((x&0xff000000ULL)<<8)|((x&0xff00000000ULL)>>8)|
((x&0xff0000000000ULL)>>24)|((x&0xff000000000000ULL)>>40)|
((x&0xff00000000000000ULL)>>56);
}

/*:16*//*17:*/

int
write_i64(FILE*ofile,i64_t*buffer,size_t count)
{
size_t i;
int res;

for(i= 0;i<count;i++)
buffer[i]= bswap_64(buffer[i]);
res= fwrite(buffer,sizeof(*buffer),count,ofile);
for(i= 0;i<count;i++)
buffer[i]= bswap_64(buffer[i]);
return res;
}

/*:17*//*18:*/

int
write_u64(FILE*ofile,u64_t*buffer,size_t count)
{
size_t i;
int res;

for(i= 0;i<count;i++)
buffer[i]= bswap_64(buffer[i]);
res= fwrite(buffer,sizeof(*buffer),count,ofile);
for(i= 0;i<count;i++)
buffer[i]= bswap_64(buffer[i]);
return res;
}

/*:18*//*19:*/

int
write_i32(FILE*ofile,i32_t*buffer,size_t count)
{
size_t i;
int res;

for(i= 0;i<count;i++)
buffer[i]= bswap_32(buffer[i]);
res= fwrite(buffer,sizeof(*buffer),count,ofile);
for(i= 0;i<count;i++)
buffer[i]= bswap_32(buffer[i]);
return res;
}

/*:19*//*20:*/

int
write_u32(FILE*ofile,u32_t*buffer,size_t count)
{
size_t i;
int res;

for(i= 0;i<count;i++)
buffer[i]= bswap_32(buffer[i]);
res= fwrite(buffer,sizeof(*buffer),count,ofile);
for(i= 0;i<count;i++)
buffer[i]= bswap_32(buffer[i]);
return res;
}

/*:20*//*21:*/

int
read_i64(FILE*ifile,i64_t*buffer,size_t count)
{
size_t i;
int res;

res= fread(buffer,sizeof(*buffer),count,ifile);
for(i= 0;i<count;i++)
buffer[i]= bswap_64(buffer[i]);
return res;
}

/*:21*//*22:*/

int
read_u64(FILE*ifile,u64_t*buffer,size_t count)
{
size_t i;
int res;

res= fread(buffer,sizeof(*buffer),count,ifile);
for(i= 0;i<count;i++)
buffer[i]= bswap_64(buffer[i]);
return res;
}

/*:22*//*23:*/

int
read_i32(FILE*ifile,i32_t*buffer,size_t count)
{
size_t i;
int res;

res= fread(buffer,sizeof(*buffer),count,ifile);
for(i= 0;i<count;i++)
buffer[i]= bswap_32(buffer[i]);
return res;
}

/*:23*//*24:*/

int
read_u32(FILE*ifile,u32_t*buffer,size_t count)
{
size_t i;
int res;

res= fread(buffer,sizeof(*buffer),count,ifile);
for(i= 0;i<count;i++)
buffer[i]= bswap_32(buffer[i]);
return res;
}

/*:24*/

#endif 


/*:14*//*25:*/

#ifdef NEED_GETLINE
#define GETL_INCR 128
ssize_t
getline(char**lineptr,size_t*n,FILE*stream)
{
int rv;

if(*n==0){
*n= GETL_INCR;
*lineptr= xmalloc(*n);
}

rv= 0;
for(;;){
int m;
m= *n-rv;
if(fgets(*lineptr+rv,m-1,stream)==NULL)break;
rv= strlen(*lineptr);
if(rv==0||(*lineptr)[rv-1]=='\n')break;
*n+= GETL_INCR;
*lineptr= xrealloc(*lineptr,*n);
}
return rv;
}
#endif

/*:25*//*26:*/

#ifdef NEED_ASPRINTF
int
vasprintf(char**ptr,const char*template,va_list ap)
{

int n,size= 32;

while(1){
va_list aq;
*ptr= xmalloc(size);

va_copy(aq,ap);
n= vsnprintf(*ptr,size,template,aq);
va_end(aq);
if(n>=0&&n<size)return n;
free(*ptr);
size= n>=0?n+1:size*2;
}
}
#endif

/*:26*//*27:*/

#ifdef NEED_ASPRINTF
int asprintf(char**ptr,const char*template,...)
{
int rv;
va_list ap;

va_start(ap,template);
rv= vasprintf(ptr,template,ap);
va_end(ap);
return rv;
}
#endif

/*:27*//*28:*/

#ifdef NEED_FNMATCH
int fnmatch(char*s,char*fname,int dummy)
{
while(*fname)fname++;
if(*(fname--)!='z')return 1;
if(*(fname--)!='g')return 1;
if(*(fname--)!='.')return 1;
return 0;
}
#endif


/*:28*//*29:*/

int
u32_cmp012(const void*x,const void*y)
{
const u32_t*xx,*yy;
xx= (const u32_t*)x;
yy= (const u32_t*)y;
if(*xx<*yy)return-1;
if(*xx> *yy)return 1;
return 0;
}

/*:29*//*30:*/

int
u32_cmp210(const void*x,const void*y)
{
const u32_t*xx,*yy;
xx= (const u32_t*)x;
yy= (const u32_t*)y;
if(*xx<*yy)return 1;
if(*xx> *yy)return-1;
return 0;
}

/*:30*//*31:*/

int
u64_cmp012(const void*x,const void*y)
{
const u64_t*xx,*yy;
xx= (const u64_t*)x;
yy= (const u64_t*)y;
if(*xx<*yy)return-1;
if(*xx> *yy)return 1;
return 0;
}

/*:31*//*32:*/

int
u64_cmp210(const void*x,const void*y)
{
const u64_t*xx,*yy;
xx= (const u64_t*)x;
yy= (const u64_t*)y;
if(*xx<*yy)return 1;
if(*xx> *yy)return-1;
return 0;
}
/*:32*/
}  /* namespace lasieve_ns */
