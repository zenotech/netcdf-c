/*
 *      Copyright 1996, University Corporation for Atmospheric Research
 *      See netcdf/COPYRIGHT file for copying and redistribution conditions.
 */

#include "config.h"
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "netcdf.h"
#include "ncs3.h"

#if 0
#define  X_ALIGN 4
#endif

/* Provide lowest level IO for S3:
1. get metadata
2. read chunk
#3. create file
#4. write chunk
*/

/**************************************************/
/* Low level curl operations */

static size_t
header_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    S3* s3 = (S3*)userdata;
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
	meta->length = strtoull(value,NULL,10);
    } else if(strcasecmp(key,"content-type")==0) {
	meta->type = strndup(value,len);
    } else if(strcasecmp(key,"last-modified")==0) {
	meta->last_modified = strndup(value,len);
    } else if(strcasecmp(key,"etag")==0) {
	meta->etag = strndup(value,len);
    } else if(strcasecmp(key,"x-amz-version-id")==0) {
	meta->version_id = strndup(value,len);
    } else if(strcasecmp(key,"server")==0) {
	meta->server = strndup(value,len);
    } else if(strcasecmp(key,"connection")==0) {
	if(strncmp(value,"open",len)==0)
	    meta->connected = 1;
	else
	    meta->connected = 0;
    } else {
	/* Ignore other headers */
    }
    return len0;
}

static size_t
WriteMemoryCallback(void *input, size_t size, size_t nmemb, void* userdata)
{
    S3* s3 = (S3*)userdata;
    size_t realsize = size * nmemb;
    S3_Range* range = &s3->range;

    if(realsize == 0)
	return realsize;

    /* Assume that initially |range->buffer| <= range->count */

    /* validate offset+realsize against count */
    if((range->offset + realsize) >= range->count) {
        /* we got too many bytes back */
	return 0;
    }
    memcpy(range->buffer+range->offset,input,realsize);
    range->offset += realsize;
    return realsize;
}

int
ncs3_read_metadata(S3* s3)
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
    return (cstat == CURLE_OK ? NC_NOERR : NC_EURL);
}

/*
Caller must ensure buffer is big enough
*/
int
ncs3_read_data(S3* s3, void* buffer, size_t count)
{
    int status = NC_NOERR;
    CURLcode cstat = CURLE_OK;
    CURL* curl = s3->curl;
    S3_Range* range = &s3->range;

    /* If metadata has not been read, do it now */
    if(!s3->meta.initialized) {
	status = ncs3_read_metadata(s3);
	if(status) goto done;
    }

    cstat = curl_easy_setopt(curl, CURLOPT_URL, (void*)s3->trueurl);
    if(cstat != CURLE_OK) goto done;

    /* send all data to this function */
    cstat = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)s3);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_HTTPGET, (long)1);// GET operation
    if(cstat != CURLE_OK) goto done;

    range->start = 0;
    range->count = count;
    range->offset = 0;
    range->buffer = buffer;

    cstat = curl_easy_perform(curl);
    if(cstat != CURLE_OK) goto done;

    /* Get the return code */
    cstat = curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&s3->code);
    if(cstat != CURLE_OK) goto done;

done:
    return (cstat == CURLE_OK ? NC_NOERR : NC_EURL);
}

/* Create S3 instance */
int
ncs3_open(S3** s3p)
{
    CURLcode cstat = CURLE_OK;
    S3* s3 = NULL;

    s3 = (S3*)calloc(1,sizeof(S3));    
    if(s3 == NULL) return CURLE_OUT_OF_MEMORY;

    /* Create a CURL instance */
    s3->curl = curl_easy_init();
    if(s3->curl == NULL) goto done;

    cstat = curl_easy_setopt(s3->curl, CURLOPT_NOPROGRESS, 1);
    if(cstat != CURLE_OK) goto done;

    cstat = curl_easy_setopt(s3->curl, CURLOPT_MAXREDIRS, 10L);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(s3->curl, CURLOPT_FOLLOWLOCATION, 1L);
    if(cstat != CURLE_OK) goto done;
    /* use a very short timeout: 10 seconds */
    cstat = curl_easy_setopt(s3->curl, CURLOPT_TIMEOUT, (long)10);
    if(cstat != CURLE_OK) goto done;
    /* fail on HTTP 400 code errors */
    cstat = curl_easy_setopt(s3->curl, CURLOPT_FAILONERROR, (long)1);
    if(cstat != CURLE_OK) goto done;

    if(s3p != NULL) *s3p = s3;

done:
    if(cstat != CURLE_OK && s3) {
	if(s3->curl)
	    (void)curl_easy_cleanup(s3->curl);
	free(s3);
        if(s3p != NULL) *s3p = NULL;
    }
    return cstat;
}

int
ncs3_free(S3* s3)
{
    S3_Metadata* md = &s3->meta;
    if(s3 == NULL) return CURLE_OK;
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
    return CURLE_OK;
}
