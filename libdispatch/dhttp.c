#include "config.h"
#include "curl/curl.h"
#include "ncdispatch.h"
#include "ncuri.h"
#include "nchttp.h"

#define MAXSERVERURL 4096

/* Define the known protocols and their manipulations */
static struct NCPROTOCOLLIST {
    char* protocol;
    char* substitute;
    int   model;
} ncprotolist[] = {
    {"http",NULL,0},
    {"https",NULL,0},
    {"file",NULL,NC_FORMATX_DAP2},
    {"dods","http",NC_FORMATX_DAP2},
    {"dodss","https",NC_FORMATX_DAP2},
    {"dap4","http",NC_FORMATX_DAP4},
    {"dap4s","https",NC_FORMATX_DAP4},
    {"s3","http",NC_FORMATX_S3},
    {"s3s","https",NC_FORMATX_S3},
    {NULL,NULL,0} /* Terminate search */
};

/* Define the default servers to ping in order;
   make the order attempt to optimize
   against future changes.
*/
static const char* default_servers[] = {
"http://remotetest.unidata.ucar.edu",
NULL
};

/* search list of servers and return first that succeeds when
   concatenated with the specified path part.
   Search list can be prefixed by the second argument.
*/
char*
NC_findtestserver(const char* path, const char** servers)
{
#ifdef USE_DAP
#ifdef ENABLE_DAP_REMOTE_TESTS
    /* NCDAP_ping is defined in libdap2/ncdap.c */
    const char** svc;
    int stat;
    char* url = (char*)malloc(MAXSERVERURL);

    if(path == NULL) path = "";
    if(strlen(path) > 0 && path[0] == '/')
	path++;

    if(servers != NULL) {
        for(svc=servers;*svc != NULL;svc++) {
            snprintf(url,MAXSERVERURL,"%s/%s",*svc,path);
            if(NC_ping(url))
                return url;
        }
    }
    /* not found in user supplied list; try defaults */
    for(svc=default_servers;*svc != NULL;svc++) {
        snprintf(url,MAXSERVERURL,"%s/%s",*svc,path);
        if(NC_ping(url))
            return url;
    }
    if(url) free(url);
#endif
#endif
    return NULL;
}


/* return 1 if path looks like a url; 0 otherwise */
int
NC_testurl(const char* path)
{
    int isurl = 0;
    NCURI* tmpurl = NULL;
    char* p;

    if(path == NULL) return 0;

    /* find leading non-blank */
    for(p=(char*)path;*p;p++) {if(*p != ' ') break;}

    /* Do some initial checking to see if this looks like a file path */
    if(*p == '/') return 0; /* probably an absolute file path */

    /* Ok, try to parse as a url */
    if(ncuriparse(path,&tmpurl)) {
	/* Do some extra testing to make sure this really is a url */
        /* Look for a known protocol */
        struct NCPROTOCOLLIST* protolist;
        for(protolist=ncprotolist;protolist->protocol;protolist++) {
	    if(strcmp(tmpurl->protocol,protolist->protocol) == 0) {
	        isurl=1;
		break;
	    }
	}
	ncurifree(tmpurl);
	return isurl;
    }
    return 0;
}

/*
Return an NC_FORMATX_... value.
Assumes that the path is known to be a url.
Return NC_FORMATX_UNDEFINED if we cannot tell.
*/

int
NC_urlmodel(const char* path)
{
    int model = NC_FORMATX_UNDEFINED;
    NCURI* tmpurl = NULL;
    struct NCPROTOCOLLIST* protolist;

    if(ncuriparse(path,&tmpurl)) {
        /* Look for a known protocol */
        struct NCPROTOCOLLIST* protolist;
        for(protolist=ncprotolist;protolist->protocol;protolist++) {
	    if(strcmp(tmpurl->protocol,protolist->protocol) == 0) {
		model = protolist->model;
		break;
	    }
	}
	ncurifree(tmpurl);
    }
    return model;
}

#ifdef USING_LIBCURL
static size_t
PingCallback(void *input, size_t size, size_t nmemb, void* userdata)
{
    size_t realsize = size * nmemb;
    return realsize;
}

int
NC_ping(const char* url)
{
    CURLcode cstat = CURLE_OK;
    CURL* curl = NULL;
    long data = 0;
    long httpcode = 200;
    int retval = 0;

    /* Create a CURL instance */
    curl = curl_easy_init();
    if(curl == NULL) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
    if(cstat != CURLE_OK) goto done;

    /* Use redirects */
    cstat = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    if (cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (cstat != CURLE_OK) goto done;
    /* use a very short timeout: 10 seconds */
    cstat = curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)10);
    if (cstat != CURLE_OK) goto done;
    /* fail on HTTP 400 code errors */
    cstat = curl_easy_setopt(curl, CURLOPT_FAILONERROR, (long)1);
    if (cstat != CURLE_OK) goto done;

    /* Try to HEAD the url */
    cstat = curl_easy_setopt(curl, CURLOPT_URL, (void*)url);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, PingCallback);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&data);
    if(cstat != CURLE_OK) goto done;
    cstat = curl_easy_perform(curl);
    if(cstat == CURLE_PARTIAL_FILE)
	    cstat = CURLE_OK;
    if(cstat != CURLE_OK) goto done;

    /* Extract the http code */
#ifdef HAVE_CURLINFO_RESPONSE_CODE
    cstat = curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&httpcode);
#else
    cstat = curl_easy_getinfo(curl,CURLINFO_HTTP_CODE,&httpcode);
#endif
    if(cstat != CURLE_OK) httpcode = 0;

done:
    retval = (cstat == CURLE_OK ? 1 : 0);
    (void)curl_easy_cleanup(curl);
    if(retval == 1) retval = (httpcode == 200 ? 1 : 0);
    return retval;
}
#endif
