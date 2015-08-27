/**
This software is released under the terms of the Apache License version 2.
For details of the license, see http://www.apache.org/licenses/LICENSE-2.0.
*/

#ifndef LIBS3_H
#define LIBS3_H

#include <curl/curl.h>

/*
S3 Metadata:
     length -- length in bytes of the object
     type -- mime type of the object content e.g. text/plain
     last_modified -- date of last modification of the object
     etag -- entity hash: hash of the object content
     version_id -- object version
     server -- server providing the object
     connected -- open(=1) | closed(=0)
*/

typedef struct S3_Metadata {
    int   initialized;
    char* bucket;
    char* object;
    /* Following come from headers */
    char* version_id;
    off_t length;
    char* type;
    char* last_modified;
    char* etag;
    char* server;
    int connected;
} S3_Metadata;


typedef struct S3_Range {
    off_t start;
    off_t count;
} S3_Range;

typedef struct S3 S3;

/* Define the error return codes */
typedef enum S3error {
S3_OK=0,
S3_ENOMEM=1,   /* malloc failure */
S3_ECURL=2,    /* some kind of curl error; exact code is in S3 struct */
S3_EHTTP=3,    /* HTTP response code was not 200; exact code is in S3 struct */
S3_EURL=4,     /* malformed URL */
S3_EAUTH=5,    /* authorization failure */
S3_EMETA=6,    /* Metadata parse or build failure */
S3_EEXIST=7,   /* S3 Object does not exist or would be overwritten*/
S3_ES3=8,      /* Generifc S3 failure */
} S3error;

extern S3error ls3_read_metadata(S3*);
extern S3error ls3_create_metadata(S3*);
extern S3error ls3_read_data(S3* s3, void* buffer, off_t start, size_t count);
extern S3error ls3_write_data(S3* s3, void* buffer, off_t start, size_t count);
extern S3error ls3_create(const char* url, S3** s3p);
extern S3error ls3_open(const char* url, S3** s3);
extern S3error ls3_close(S3* s3);
extern int ls3_delete(const string url);

/* S3 Accessors */
extern CURL* ls3_get_curl(S3*);
extern char* ls3_get_s3url(S3*);
extern char* ls3_get_trueurl(S3*);
extern S3_Metadata ls3_get_meta(S3*);
extern long ls3_get_code(S3*);
extern S3_Range ls3_get_range(S3*);
extern off_t ls3_get_nread(S3*);

#endif /*LIBS3_H*/
