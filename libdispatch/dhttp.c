#include "config.h"
#include "curl/curl.h"
#include "ncdispatch.h"
#include "ncuri.h"
#include "nclist.h"
#include "nchttp.h"

#define MAXSERVERURL 4096
#define MAXMODELS 4


static NClist* registry = NULL;
static NC_protocol_test default_protocol = NULL;

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

int
NC_register_protocol(NC_protocol_test callback, int dfalt)
{
    if(callback == NULL)
	return NC_EINVAL;
    if(registry == NULL)
	registry = nclistnew();
    nclistpush(registry,(void*)callback);
    if(dfalt)
	default_protocol = callback;
    return NC_NOERR;
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
    isurl = ncuriparse(path,&tmpurl);
    ncurifree(tmpurl);
    return isurl;
}

/*
Return an NC_FORMATX_... value  and possible version in modelp and versionp.
Return NC_FORMATX_UNDEFINED if we cannot tell.
*/

int
NC_urlmodel(const char* path, int* versionp)
{
    NCURI* tmpurl = NULL;
    int i, match;
    int isurl = ncuriparse(path,&tmpurl);
    int model = 0;

    if(!isurl) return NC_EINVAL;
    if(registry == NULL || nclistlength(registry) == 0)
	return NC_EURL;

    for(i=0;i<nclistlength(registry);i++) {
	NC_protocol_test callback = (NC_protocol_test)nclistget(registry,i);
	match = callback(0,tmpurl,&model,versionp);
	if(match) break;
    }
    if(!match) {
	model =NC_FORMATX_UNDEFINED;
	default_protocol(1,tmpurl,&model,versionp);
    }
    ncurifree(tmpurl);
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
