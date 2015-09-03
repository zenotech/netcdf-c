/**
This software is released under the terms of the Apache License version 2.
For details of the license, see http://www.apache.org/licenses/LICENSE-2.0.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#if 0
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
//#include <sys/types.h>
#if defined HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#endif /*0*/

#include "libs3.h"
#include "libs3uri.h"

#define TRUETEMPLATE "http://%s.aws.amazon.com/%s"
#define S3TEMPLATE "s3://%s/%s"

typedef struct RWcallback {
    S3* s3;
    S3_Range range;
    void* buffer; /* Assert |buffer| <= count */
    size_t offset; /* read/write point into buffer */
} RWcallback;

struct S3 {
    CURL* curl;
    char* s3url;
    char* trueurl;
    long long eof; /* last offset actually written not necessarily == ontent-length*/
    int readonly;
    S3_Metadata meta;
    long code; /* last code received */
    S3_Range range;
    size_t iocount; /* # of bytes read/written */
};

/*Forward*/
static char* s3totrue(const char* s3url, char** bucket, char** object);
static void freeheaders(char** headers);
static S3error errcvt(CURLcode cstat);
static int s3strncmp(const char* s1, const char* s2, size_t len);
static char* s3strndup(const char* s, size_t len);

/**************************************************/
/* Provide low level IO for S3:
1. get metadata
2. read chunk
4. write chunk
3. create file
5. open
6. close
*/

/**************************************************/
/* Low level curl operations */

static size_t
WriteMemoryCallback(void *input, size_t size, size_t nmemb, void* userdata)
{
    RWcallback* rw = (RWcallback*)userdata;
    size_t realsize = size * nmemb;

    if(realsize == 0)
	return realsize;

    /* Assume that initially |range->buffer| <= range->count */

    /* validate */
    if((realsize + rw->offset) >= rw->range.count) {
        /* we got too many bytes back */
	return 0;
    }
    memcpy(rw->buffer+rw->offset,input,realsize);
    rw->offset += realsize;
    return realsize;
}

static size_t
ReadCallback(void* buffer, size_t size, size_t nmemb, void* userdata)
{
    RWcallback* rw = (RWcallback*)userdata;
    size_t realsize = size * nmemb;
    size_t towrite = 0;

    towrite = (rw->range.count - rw->offset);
    if(towrite > realsize)
	towrite = realsize;
    if(towrite == 0)
	return 0; /* EOF */
    memcpy(buffer, rw->buffer+rw->offset, towrite);
    rw->offset += towrite;
    return towrite;
}

static size_t
header_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    RWcallback* rw = (RWcallback*)userdata;
    S3* s3 = rw->s3;
    S3_Metadata* meta = &s3->meta;
    size_t len0 = size*nitems;
    char* key = buffer;
    char* value;
    char* p;
    int c;
    int len = len0;

    buffer[len-1] = '\0'; /* will this wipe out the last value char? */
    for(p=buffer;(c=*p);p++) {
	if(c <= ' ' || c == ':') break;	    	    
    }
    if(c == '\0')
	return 0;
    *p++ = '\0';
    while((c=*p++) == ' ');
    value = p;
    len -= (p - buffer);
    if(strcasecmp(key,"content-length")==0) {
	meta->content_length = strtoull(value,NULL,10);
    } else if(strcasecmp(key,"content-type")==0) {
	meta->type = s3strndup(value,len);
    } else if(strcasecmp(key,"last-modified")==0) {
	meta->last_modified = s3strndup(value,len);
    } else if(strcasecmp(key,"etag")==0) {
	meta->etag = s3strndup(value,len);
    } else if(strcasecmp(key,"x-amz-version-id")==0) {
	meta->version_id = s3strndup(value,len);
    } else if(strcasecmp(key,"server")==0) {
	meta->server = s3strndup(value,len);
    } else if(strcasecmp(key,"connection")==0) {
	if(s3strncmp(value,"open",len)==0)
	    meta->connected = 1;
	else
	    meta->connected = 0;
    } else {
	/* Ignore other headers */
    }
    return len0;
}

/* Convert metadata to headers */
static char**
buildcreateheaders(S3_Metadata* md)
{
#define NHDRS 3
    int i;
    char** hdrs = calloc((1+NHDRS),sizeof(char*));
    char line[8192];

    if(hdrs == NULL) return NULL;
    
    for(i=0;i<NHDRS;i++) {
	line[0] = '\0';
        switch (i) {
	case 0: /*content-type*/
	    if(md->type != NULL)
	        snprintf(line,sizeof(line),"Content-Type: %s",
                                            md->type);
		break;
	case 1: /*content-length*/
	    if(md->content_length >= 0)
	        snprintf(line,sizeof(line),"Content-Length: %lld",
                                            md->content_length);
		break;
	case 2: /*version_id*/
	    if(md->version_id != NULL)
	        snprintf(line,sizeof(line),"x-amz-version-id: %s",
                                            md->version_id);
		break;
	}	
	if(line[0] != '\0')
	    hdrs[i] = strdup(line);
    }
    return hdrs;
}

static S3error
curlclose(CURL* curl)
{
    if(curl != NULL)
        curl_easy_cleanup(curl);
    return S3_OK;
}

static S3error
curlopen(CURL** curlp)
{
    S3error stat = S3_OK;
    CURLcode cstat = CURLE_OK;
    CURL* curl = NULL;

    /* Create a CURL instance */
    curl = curl_easy_init();
    if(curl == NULL) return S3_ECURL;

    cstat = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    if(cstat != CURLE_OK) goto fail;
    cstat = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    if(cstat != CURLE_OK) goto fail;
    cstat = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if(cstat != CURLE_OK) goto fail;
    /* use a very short timeout: 10 seconds */
    cstat = curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)10);
    if(cstat != CURLE_OK) goto fail;
    /* fail on HTTP 400 code errors */
    cstat = curl_easy_setopt(curl, CURLOPT_FAILONERROR, (long)1);
    if(cstat != CURLE_OK) goto fail;
    if(curlp) *curlp = curl;
    return S3_OK;

fail:
    if(curl != NULL)
        curlclose(curl);
    return errcvt(cstat);
}

S3error
ls3_read_metadata(S3* s3)
{
    CURLcode cstat = CURLE_OK;
    CURL* curl = s3->curl;

    cstat = curl_easy_setopt(curl, CURLOPT_URL, (void*)s3->trueurl);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)s3);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback); 
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_NOBODY, (long)1);// HEAD operation
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_perform(curl);
    if(cstat != CURLE_OK) goto done;
    /* Get the return code */
    cstat = curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&s3->code);
    if(cstat != CURLE_OK) goto done;
    s3->meta.initialized = 1;
done:
    return errcvt(cstat);
}

/*
Caller must ensure buffer is big enough
*/
S3error
ls3_read_data(S3* s3, void* buffer, long long start, size_t count)
{
    int status = S3_OK;
    CURLcode cstat = CURLE_OK;
    S3error s3stat = S3_OK;
    CURL* curl = s3->curl;
    char srange[1024];
    RWcallback rw;

    s3->range.start = start;
    s3->range.start = count;
    s3->iocount = 0;

    /* If metadata has not been read, do it now */
    if(!s3->meta.initialized) {
	s3stat = ls3_read_metadata(s3);
	if(s3stat) goto done;
    }

    cstat = curl_easy_setopt(curl, CURLOPT_URL, (void*)s3->trueurl);
    if(cstat != CURLE_OK) goto done;

    rw.s3 = s3;
    rw.range = s3->range;
    rw.offset = 0;
    rw.buffer = buffer;

    snprintf(srange,sizeof(srange),"%lld-%lld",
             rw.range.start,rw.range.start+rw.range.count);

    /* send all data to this function */
    cstat = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&rw);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_HTTPGET, (long)1);// GET operation
    if(cstat != CURLE_OK) goto done;

    cstat = curl_easy_setopt(curl, CURLOPT_RANGE, (void*)srange);
    if(cstat != CURLE_OK) goto done;

    cstat = curl_easy_perform(curl);
    if(cstat != CURLE_OK) goto done;

    /* save read count */
    s3->iocount = rw.offset;

    /* Get the return code */
    cstat = curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&s3->code);
    if(cstat != CURLE_OK) goto done;

    /* Update file length */
    if(s3->eof < (start + count))
	s3->eof = (start+count);    
    if(s3->meta.content_length < s3->eof)
        s3->meta.content_length = s3->eof;

done:
    return errcvt(cstat);
}

/*
Caller must ensure buffer is big enough
*/
S3error
ls3_write_data(S3* s3, const void* buffer, long long start, size_t count)
{
    int status = S3_OK;
    CURLcode cstat = CURLE_OK;
    CURL* curl = s3->curl;
    char srange[1024];
    RWcallback rw;

    if(s3->readonly)
	return S3_EPERM;

    assert(s3->meta.initialized);

    s3->range.start = start;
    s3->range.start = count;
    s3->iocount = 0;

    cstat = curl_easy_setopt(curl, CURLOPT_URL, (void*)s3->trueurl);
    if(cstat != CURLE_OK) goto done;

    /* send all data to this function */
    cstat = curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadCallback);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_READDATA, (void *)s3);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_PUT, (long)1);// PUT operation
    if(cstat != CURLE_OK) goto done;

    rw.s3 = s3;
    rw.range = s3->range;
    rw.offset = 0;
    rw.buffer = (void*)buffer;

    snprintf(srange,sizeof(srange),"%lld-%lld",
	     rw.range.start,rw.range.start+rw.range.count);
    cstat = curl_easy_setopt(curl, CURLOPT_RANGE, (void*)srange);
    if(cstat != CURLE_OK) goto done;

    cstat = curl_easy_perform(curl);
    if(cstat != CURLE_OK) goto done;

    /* Get count of bytes writtent */
    s3->iocount = rw.offset;

    /* Get the return code */
    cstat = curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&s3->code);
    if(cstat != CURLE_OK) goto done;

    /* update file length */
    if(s3->eof < (start + count))
	s3->eof = (start+count);    
    if(s3->meta.content_length < s3->eof)
        s3->meta.content_length = s3->eof;

done:
    return errcvt(cstat);
}

S3error
s3create(const char* url, S3* s3)
{
    S3error stat = S3_OK;
    CURLcode cstat = CURLE_OK;
    S3_Metadata* md = NULL;
    CURL* curl = NULL;
    int ustat = 0;

    /* Now, fill in the metadata prior to creation */
    md = &s3->meta;
    memset(md,0,sizeof(S3_Metadata));
    
    {/* Fill in headers */
        char** headers = NULL;
	char** pp = NULL;
        struct curl_slist *curlheaders=NULL; 

        headers = buildcreateheaders(md);
        if(headers == NULL)
	    {stat = S3_EMETA; goto done;}
        /* Install the headers */
        for(pp=headers;*pp;pp++) {
	    curlheaders = curl_slist_append(curlheaders, *pp);
        }    
	freeheaders(headers);
        /* set our custom set of headers */
        cstat = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlheaders);
    }
    cstat = curl_easy_perform(curl);
    if(cstat != CURLE_OK) goto done;
    /* Get the return code */
    cstat = curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&s3->code);
    if(cstat != CURLE_OK) goto done;

    /* Rebuild the complete metadata */
    md->initialized = 0;
    stat = ls3_read_metadata(s3);

    s3->eof = md->content_length;

done:
    if(cstat && !stat)
	stat = errcvt(cstat);
    return stat;
}

/**
    mode - one of
	"r" (readonly) 
	"w" (truncate & read/write)
	"rw" (read/write)
*/

S3error
ls3_open(const char* url, const char* mode, S3** s3p)
{
    CURLcode cstat = CURLE_OK;
    S3error s3stat = S3_OK;
    S3* s3 = NULL;
    S3_Metadata* md;
    int readonly,truncate;

    if(mode == NULL) mode = "r";
    readonly = 0;
    truncate = 0;
    if(strcmp(mode,"r")==0)
	{readonly = 1; truncate = 0;}
    else if(strcmp(mode,"w")==0)
	{readonly = 0; truncate = 1;}
    else if(strcmp(mode,"rw")==0)
	{readonly = 0; truncate = 0;}
    else
	return S3_EMODE;

    if(truncate) {
	/* delete then create */
	s3stat = ls3_delete(url);
	if(s3stat == S3_OK)
	    s3stat = s3create(url,s3);		
	if(s3stat != S3_OK) goto done;
    }

    s3 = (S3*)calloc(1,sizeof(S3));    
    if(s3 == NULL) return S3_ENOMEM;

    /* Create a CURL instance */
    s3stat = curlopen(&s3->curl);
    if(s3stat) goto done;

    /* Now, fill in S3 and S3 metadata */
    s3 = (S3*)calloc(1,sizeof(S3));    
    if(s3 == NULL) {s3stat = S3_ENOMEM; goto done;}

    s3->s3url = strdup(url);
    md = &s3->meta;
    s3->trueurl = s3totrue(url,&md->bucket,&md->object);
    if(s3->trueurl == NULL)
	{s3stat = S3_EURL; goto done;}
    s3->readonly = readonly;

    /* Attempt to read the metadata */
    s3stat = ls3_read_metadata(s3);
    if(s3stat) goto done;    
    /* Set eof to content_length */
    s3->eof = md->content_length;

    if(s3p != NULL) *s3p = s3;

done:
    if(cstat != CURLE_OK && s3) {
	if(s3->curl)
	    (void)curl_easy_cleanup(s3->curl);
	free(s3);
        if(s3p != NULL) *s3p = NULL;
    }
    return errcvt(cstat);
}

S3error
ls3_close(S3* s3)
{
    S3_Metadata* md = &s3->meta;
    if(s3 == NULL) return S3_OK;
    (void)curl_easy_cleanup(s3->curl);
    if(s3->s3url) free(s3->s3url);
    if(s3->trueurl) free(s3->trueurl);
    if(md->bucket) free(md->bucket);
    if(md->object) free(md->object);
    if(md->version_id) free(md->version_id);
    if(md->type) free(md->type);
    if(md->last_modified) free(md->last_modified);
    if(md->etag) free(md->etag);
    if(md->server) free(md->server);
    free(s3);        
    return S3_OK;
}

int
ls3_delete(const char* url)
{
    S3error stat = S3_OK;
    S3* s3 = NULL;
    CURLcode cstat = CURLE_OK;

    /* Make sure the object exists */
    stat = ls3_open(url, "rw", &s3);
    if(stat || ls3_get_code(s3) >= 400)
	return stat; 

    /* Prepare to delete */
    cstat = curl_easy_setopt(s3->curl,CURLOPT_CUSTOMREQUEST,"delete");
    if(cstat == CURLE_OK)
        cstat = curl_easy_perform(s3->curl);
    (void)ls3_close(s3);
    if(cstat) stat = errcvt(cstat);
    return stat;
}

/* S3 Accessors */

long long
ls3_get_eof(S3* s3)
{
    return s3->eof;
}

int
ls3_set_length(S3* s3)
{
    return S3_OK;
}

CURL*
ls3_get_curl(S3* s3)
{
    return s3->curl;
}

const char*
ls3_get_s3url(S3* s3)
{
    return s3->s3url;
}

const char*
ls3_get_trueurl(S3* s3)
{
    return s3->trueurl;
}

S3_Metadata*
ls3_get_meta(S3* s3)
{
    return &s3->meta;
}

long
ls3_get_code(S3* s3)
{
    return s3->code;
}

long long
ls3_get_iocount(S3* s3)
{
    return s3->iocount;
}

/**************************************************/
/* Utilities */

static char*
s3totrue(const char* s3url, char** bucket, char** object)
{
    char trueurl[8192];
    LS3URI* tmpuri = NULL;
    if(!ls3_uriparse(s3url,&tmpuri)) {
        ls3_urifree(tmpuri);
	return NULL;
    }
    snprintf(trueurl,sizeof(trueurl),TRUETEMPLATE,tmpuri->host,tmpuri->file);
    if(bucket) *bucket = strdup(tmpuri->host);
    if(object) *object = strdup(tmpuri->file);
    ls3_urifree(tmpuri);
    return strdup(trueurl);
}

#if 0
static char*
truetos3(const char* turl)
{
    char s3url[8192];
    char bucket[8192];
    LS3URI* tmpuri = NULL;
    char* p;
    size_t len;

    if(!ls3_uriparse(turl,&tmpuri))
	{ls3_urifree(tmpuri); return NULL;}
    p = strchr(tmpuri->host,'.');
    if(p == NULL || p == tmpuri->host)
	{ls3_urifree(tmpuri); return NULL;}
    len = (size_t)(p - tmpuri->host);
    s3strncpy(bucket,tmpuri->host,len);
    snprintf(s3url,sizeof(s3url),S3TEMPLATE,bucket,tmpuri->file);
    ls3_urifree(tmpuri);
    return strdup(s3url);
}
#endif

static S3error
errcvt(CURLcode cstat)
{
    switch (cstat) {
    case CURLE_OK: return S3_OK;
    case CURLE_OUT_OF_MEMORY: return S3_ENOMEM;
    default: break;
    }
    return S3_ECURL;
}

static void
freeheaders(char** headers)
{
    char** pp = headers;
    if(pp == NULL) return;
    while(*pp) {free(pp); pp++;}
    free(headers);
}

/**************************************************/

/* Utilities */

/* Create private versions of some string functions */

static char*
s3strndup(const char* s, size_t len)
{
    char* dup;
    if(s == NULL) return NULL;
    dup = (char*)malloc(len+1);
    if(dup == NULL)
	return NULL;
    memcpy((void*)dup,s,len);
    dup[len] = '\0';
    return dup;
}

#if 0
static void
s3strncpy(char* dst, const char* src, size_t len)
{
    if(dst == NULL) return;
    if(len == 0) {dst[0] = '\0' ; return;}
    memcpy((void*)dst,src,len);
    dst[len] = '\0';
}
#endif

/* Do not trust strncmp semantics; this one
   compares upto len chars or to null terminators */
static int
s3strncmp(const char* s1, const char* s2, size_t len)
{
    const char *p,*q;
    if(s1 == s2) return 0;
    if(s1 == NULL) return -1;
    if(s2 == NULL) return +1;
    for(p=s1,q=s2;len > 0;p++,q++,len--) {
	if(*p == 0 && *q == 0) return 0; /* *p == *q == 0 */
	if(*p != *q)
	    return (*p - *q);	
    }
    /* 1st len chars are same */
    return 0;
}

