/**
This software is released under the terms of the Apache License version 2.
For details of the license, see http://www.apache.org/licenses/LICENSE-2.0.
*/

#ifndef LIBS3URI_H
#define LIBS3URI_H

/*! This is an open structure meaning it is ok to directly access its fields*/
typedef struct LS3URI {
    char* uri;        /* as passed by the caller */
    char* params;     /* all params */
    char** paramlist;    /*!<null terminated list */
    char* constraint; /*!< projection+selection */
    char* projection; /*!< without leading '?'*/
    char* selection;  /*!< with leading '&'*/
    char* strings;    /* first char of strings is always '\0' */
    /* Following all point into the strings field=>don't free */
    char* protocol;
    char* user; /* from user:password@ */
    char* password; /* from user:password@ */
    char* host;	      /*!< host*/
    char* port;	      /*!< host */
    char* file;	      /*!< file */
} LS3URI;

/* Define flags to control what is is included in build*/
#define S3URICONSTRAINTS	 1
#define S3URIUSERPWD	  	 2
#define S3URIPREFIXPARAMS  	 4
#define S3URISUFFIXPARAMS	 8
#define S3URIPARAMS	  	S3URIPREFIXPARAMS
#define S3URIEENODE		16 /* If output should be encoded */
#define S3URISTD	  	(S3URICONSTRAINTS|S3URIUSERPWD)

#if defined(__cplusplus)
extern "C" {
#endif

extern int ls3_uriparse(const char* s, LS3URI** ls3uri);
extern void ls3_urifree(LS3URI* ls3uri);

/* Replace the constraints */
extern void ls3_urisetconstraints(LS3URI*,const char* constraints);

/* Construct a complete S3 URI; caller frees returned string */
extern char* ls3_uribuild(LS3URI*,const char* prefix, const char* suffix, int flags);

/* Param Management */
extern int ls3_uridecodeparams(LS3URI* ls3uri);
extern int ls3_urisetparams(LS3URI* ls3uri,const char*);

/*! 0 result => entry not found; 1=>found; result holds value (may be null).
    In any case, the result is imutable and should not be free'd.
*/
extern int ls3_urilookup(LS3URI*, const char* param, const char** result);

extern char* ls3_uriencode(char* s, char* allowable);
extern char* ls3_uridecode(char* s);
extern char* ls3_uridecodeonly(char* s, char*);

extern char* ls3_strndup(const char* s, size_t len);

#if defined(__cplusplus)
}
#endif

#endif /*LIBS3URI_H*/
