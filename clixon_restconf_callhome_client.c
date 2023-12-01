/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2022 Olof Hagsand and Rubicon Communications, LLC (Netgate)

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****

  See RFC 8071 NETCONF Call Home and RESTCONF Call Home

   device/server                               client
  +-----------------+   1) tcp connect   +-----------------+
  | clixon_restconf | ---------------->  | callhome-client |   <------  3) HTTP
  |                 |   2) tls           |                 |
  +-----------------+ <---------------   +-----------------+
  
  The callhome-client listens on accept, when connect comes in, creates data socket and sends
  RESTCONF GET to server, then re-waits for new accepts.
  When accepting a connection, send HTTP data from input or -f <file> tehn wait for reply
  Reply is matched with -e <expectfile> or printed on stdout

  Tracing events on stdout using:
  Accept:<n> at t=<sec>                # where <n> is connection nr, <sec> is time since start of program
  Close: <n> <where> <sec> at t=<sec>  # where <n> is connection nr, <where> is local or remote, <sec> is time since start of connection
  Reply: <n> t=<sec> [\n<msg>\n]       # where <n> is nr data reply from start, <sec> is time since start of connection
  Exit: <function>                     # where <reason> is which exit point (for debugging)

  Timeline:
      w
  <-------------->      
            a0   d0   d1                 a1   d0   d1
  ----------|----|----|------------------|----|----|------------------|

   ai Accepted connect from server
   di Reply from server
   n  Number of ai:s, 0 means no limit (_accepts)
   D  Timeout of di:s (1st request sent at ai, sent back-to-back or with 1sec interval) (_data_timeout_s)
   idle? If set do not close after D timeout
   t  Wait for accept, exit if no accepts (default: 60s), just a safety for deadlocks (_accept_timeout_s)
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <netdb.h>      /* gethostbyname */
#include <arpa/inet.h>  /* inet_pton */
#include <netinet/tcp.h> /* TCP_NODELAY */

#include <openssl/ssl.h>

/* cligen */
#include <cligen/cligen.h>

/* clixon */
#include "clixon/clixon.h"

#define UTIL_TLS_OPTS "hD:f:F:a:p:c:C:k:n:N:it:d:e:"

#define RESTCONF_CH_TLS 4336

/* User struct for context / accept  */
typedef struct {
    int              ta_ss;       /* Accept socket */
    SSL_CTX         *ta_ctx;      /* SSL context */
    struct timeval   ta_t0;       /* Program start */
} tls_accept_handle;

/* User connection-specific data handle  */
typedef struct {
    int              sd_s;   /* data socket */
    SSL             *sd_ssl; /* SSL connection data */
    struct timeval   sd_t0;  /* Start of connection, eg accept call*/
} tls_session_data;

/* Lots of global variables here, alt pass them between ta and sd structs
 */
/* Input data file for HTTP request data */
static FILE *_input_file = NULL;

/* Expected accepts */
static int _accepts = 1;

/* Number of accepts */
static int         _n_accepts = 0;

/* After accepting a socket, a request is sent to the server. The handle the data socket as follows:
 * 0: close after first reply
 * -1: dont close after reply, (remote side may close)
 * s>0: send new requests during <s> seconds after accept, then dont close
 */
static int _idle = 0;

/* Timeout in seconds after each accept, if fired just exit */
static int _accept_timeout_s = 60;

/* Timeout of data requests (1st request sent at accept, sent back-to-back / 1sec interval) 
 * Note: uses blockling timeout 100ms
 */
static int _data_timeout_s = 0;

/* Event trace, 1: terse (Accept:/Reply:/Close:) 2: full (data payload) */
static int _event_trace = 0;
static FILE *_event_f = NULL; /* set to stdout in main */

/*! Create and bind stream socket
 *
 * @param[in]  sa       Socketaddress
 * @param[in]  sa_len   Length of sa. Tecynicaliyu to be independent of sockaddr sa_len
 * @param[in]  backlog  Listen backlog, queie of pending connections
 * @param[out] sock     Server socket (bound for accept)
 * @retval     0        OK
 * @retval    -1        Error
 */
int
callhome_bind(struct sockaddr *sa,
              size_t           sin_len,
              int              backlog,
              int             *sock)
{
    int    retval = -1;
    int    s = -1;
    int    on = 1;

    if (sock == NULL){
        errno = EINVAL;
        perror("sock");
        goto done;
    }
    /* create inet socket */
    if ((s = socket(sa->sa_family, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        goto done;
    }
    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on)) == -1) {
        perror("setsockopt SO_KEEPALIVE");
        goto done;
    }
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void *)&on, sizeof(on)) == -1) {
        perror("setsockopt SO_REUSEADDR");
        goto done;
    }
    /* only bind ipv6, otherwise it may bind to ipv4 as well which is strange but seems default */
    if (sa->sa_family == AF_INET6 &&
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) == -1) {
        perror("setsockopt IPPROTO_IPV6");
        goto done;
    }
    if (bind(s, sa, sin_len) == -1) {
        perror("bind");
        goto done;
    }
    if (listen(s, backlog) < 0){
        perror("listen");
        goto done;
    }
    if (sock)
        *sock = s;
    retval = 0;
 done:
    if (retval != 0 && s != -1)
        close(s);
    return retval;
}

/*! read data from file return a malloced buffer
 *
 * Note same file is reread multiple times: same request/reply is made each iteration
 * Also, the file read is limited to 1024 bytes
 */
static int
read_data_file(FILE   *fe,
               char  **bufp,
               size_t *lenp)
{
    int    retval = -1;
    char  *buf = NULL;
    int    buflen = 1024; /* start size */
    char   ch;
    size_t len = 0;
    int    ret;

    if ((buf = malloc(buflen)) == NULL){
        clicon_err(OE_UNIX, errno, "malloc");
        goto done;
    }
    memset(buf, 0, buflen);
    /* Start file form beginning */
    rewind(fe);
    while (1){
        if ((ret = fread(&ch, 1, 1, fe)) < 0){
            clicon_err(OE_JSON, errno, "fread");
            goto done;
        }
        if (ret == 0)
            break;
        buf[len++] = ch;
        // XXX No realloc, can overflow
    }
    *bufp = buf;
    *lenp = len;
    retval = 0;
 done:
    return retval;
}

/*! Read data from file/stdin and write to TLS data socket
 */
static int
tls_write_file(FILE *fp,
               SSL  *ssl)
{
    int    retval = -1;
    char  *buf = NULL;
    size_t len = 0;
    int    ret;
    int    sslerr;

    clixon_debug(CLIXON_DBG_DEFAULT, "%s", __FUNCTION__);
    if (read_data_file(fp, &buf, &len) < 0)
        goto done;
    if ((ret = SSL_write(ssl, buf, len)) < 1){
        sslerr = SSL_get_error(ssl, ret);
        clixon_debug(CLIXON_DBG_DEFAULT, "%s SSL_write() n:%d errno:%d sslerr:%d", __FUNCTION__, ret, errno, sslerr);
    }
    retval = 0;
 done:
    if (buf)
        free(buf);
    return retval;
}

/*! Client data socket, receive reply from server
 *
 * Print info on stdout
 * If keep_open = 0, then close socket directly after 1st reply (client close)
 * If keep_open = 1, then keep socket open (server close)
 */
static int
tls_server_reply_cb(int   s,
                    void *arg)
{
    int            retval = -1;
    tls_session_data *sd = (tls_session_data *)arg;
    SSL           *ssl;
    char           buf[1024];
    int            n;
    char          *expbuf = NULL;
    struct timeval now;
    struct timeval td;
    static int     seq = 0; // from start

    //    clixon_debug(CLIXON_DBG_DEFAULT, "%s", __FUNCTION__);
    ssl = sd->sd_ssl;
    /* get reply & decrypt */
    if ((n = SSL_read(ssl, buf, sizeof(buf))) < 0){
        clicon_err(OE_XML, errno, "SSL_read");
        goto done;
    }
    clixon_debug(CLIXON_DBG_DEFAULT, "%s n:%d", __FUNCTION__, n);
    gettimeofday(&now, NULL);
    timersub(&now, &sd->sd_t0, &td); /* from start of connection */
    if (n == 0){ /* Server closed socket */
        SSL_free(ssl);
        clixon_event_unreg_fd(s, tls_server_reply_cb);
        if (_event_trace)
            fprintf(_event_f, "Close: %d remote at t=%lu\n", _n_accepts, td.tv_sec);
        close(s);
        free(sd);
        if (_accepts == 0)
            ;
        else if (_accepts == 1){
            clixon_exit_set(1); /* XXX more elaborate logic: 1) continue request, 2) close and accept new */
            fprintf(_event_f, "Exit: %s remote\n", __FUNCTION__);
        }
        else
            _accepts--;
        goto ok;
    }
    seq++;
    buf[n] = 0;
    if (_event_trace){
        fprintf(_event_f, "Reply: %d t=%lu\n", seq, td.tv_sec);
        if (_event_trace > 1)
            fprintf(_event_f, "%s\n", buf);
    }
    /* See if we should send more requests on this socket */
    if (sd->sd_t0.tv_sec + _data_timeout_s > now.tv_sec){
        /* Send another packet */
        usleep(100000); /* XXX This is a blocking timeout */
        /* Write HTTP request on socket */
        if (tls_write_file(_input_file, sd->sd_ssl) < 0)
            goto done;
    }
    else if (!_idle){
        clixon_debug(CLIXON_DBG_DEFAULT, "%s idle", __FUNCTION__);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        clixon_event_unreg_fd(s, tls_server_reply_cb);
        if (_event_trace)
            fprintf(_event_f, "Close: %d local at t=%lu\n", _n_accepts, td.tv_sec);
        close(s);
        if (_accepts == 0)
            ;
        else if (_accepts == 1){
            clixon_exit_set(1); /* XXX more elaborate logic: 1) continue request, 2) close and accept new */
            fprintf(_event_f, "Exit: %s idle\n", __FUNCTION__);
        }
        else
            _accepts--;
        free(sd);
    }
 ok:
    retval = 0;
 done:
    if (expbuf)
        free(expbuf);
    clixon_debug(CLIXON_DBG_DEFAULT, "%s ret:%d", __FUNCTION__, retval);
    return retval;
}

/*! Create ssl connection, select alpn, connect and verify
 */
static int
tls_ssl_init_connect(SSL_CTX *ctx,
                     int      s,
                     SSL    **sslp)
{
    int           retval = -1;
    SSL          *ssl = NULL;
    unsigned char protos[10];
    int           ret;
    int           verify;
    int           sslerr;

    /* create new SSL connection state */
    if ((ssl = SSL_new(ctx)) == NULL){
        clicon_err(OE_SSL, 0, "SSL_new.");
        goto done;
    }
    SSL_set_fd(ssl, s);    /* attach the socket descriptor */
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

    protos[0] = 8;
    strncpy((char*)&protos[1], "http/1.1",  9);
    if ((retval = SSL_set_alpn_protos(ssl, protos, 9)) != 0){
        clicon_err(OE_SSL, retval, "SSL_set_alpn_protos.");
        goto done;
    }
#if 0
    SSL_get0_next_proto_negotiated(conn_.tls.ssl, &next_proto, &next_proto_len);
    SSL_get0_alpn_selected(conn_.tls.ssl, &next_proto, &next_proto_len);
#endif

    /* perform the connection
       TLSEXT_TYPE_application_layer_protocol_negotiation
       int SSL_set_alpn_protos(SSL *ssl, const unsigned char *protos,
                         unsigned int protos_len);
                         see 
    https://www.openssl.org/docs/man3.0/man3/SSL_CTX_set_alpn_select_cb.html
    */
  if ((ret = SSL_connect(ssl)) < 1){
        sslerr = SSL_get_error(ssl, ret);
        clixon_debug(CLIXON_DBG_DEFAULT, "%s SSL_read() n:%d errno:%d sslerr:%d", __FUNCTION__, ret, errno, sslerr);

        switch (sslerr){
        case SSL_ERROR_SSL:                  /* 1 */
            goto done;
            break;
        default:
            clicon_err(OE_XML, errno, "SSL_connect");
            goto done;
            break;
        }
    }
    /* check certificate verification result */
    verify = SSL_get_verify_result(ssl);
    switch (verify) {
    case X509_V_OK:
        break;
    default:
        clicon_err(OE_SSL, errno, "verify problems: %d", verify);
        goto done;
    }
    *sslp = ssl;
    retval = 0;
 done:
    return retval;
}

static int
tls_timeout_cb(int   fd,
               void *arg)
{
    fprintf(_event_f, "Exit: %s\n", __FUNCTION__);
    exit(200);
}

/*! Timeout in seconds after each accept, if fired just exit 
 */
static int
tls_client_timeout(void *arg)
{
    int            retval = -1;
    struct timeval now;
    struct timeval t;
    struct timeval t1 = {0, 0};

    /* Unregister existing timeout */
    clixon_event_unreg_timeout(tls_timeout_cb, arg);
    /* Set timeout */
    gettimeofday(&now, NULL);
    t1.tv_sec = _accept_timeout_s;
    timeradd(&now, &t1, &t);
    if (clixon_event_reg_timeout(t,
                                 tls_timeout_cb,
                                 arg,
                                 "tls client timeout") < 0)
        goto done;
    retval = 0;
 done:
    return retval;
}

/*! Callhome-server accept socket
 */
static int
tls_server_accept_cb(int   ss,
                     void *arg)
{
    int                retval = -1;
    tls_accept_handle *ta = (tls_accept_handle *)arg;
    tls_session_data  *sd = NULL;
    int                s;
    struct sockaddr    from = {0,};
    socklen_t          len;
    SSL               *ssl = NULL;
    struct timeval     td;

    clixon_debug(CLIXON_DBG_DEFAULT, "%s", __FUNCTION__);
    len = sizeof(from);
    if ((s = accept(ss, &from, &len)) < 0){
        perror("accept");
        goto done;
    }
    clixon_debug(CLIXON_DBG_DEFAULT, "accepted");
    if (tls_ssl_init_connect(ta->ta_ctx, s, &ssl) < 0)
        goto done;
    clixon_debug(CLIXON_DBG_DEFAULT, "connected");
    if ((sd = malloc(sizeof(*sd))) == NULL){
        clicon_err(OE_UNIX, errno, "malloc");
        goto done;
    }
    memset(sd, 0, sizeof(*sd));
    sd->sd_s = s;
    sd->sd_ssl = ssl;
    gettimeofday(&sd->sd_t0, NULL);
    timersub(&sd->sd_t0, &ta->ta_t0, &td); /* from start of connection */
    _n_accepts++;
    if (_event_trace)
        fprintf(_event_f, "Accept: %d at t=%lu\n", _n_accepts, td.tv_sec);

    /* Always write one HTTP request on socket, maybe more if _data_timeout_s > 0 */
    if (tls_write_file(_input_file, ssl) < 0)
        goto done;
    /* register callback for reply */
    if (clixon_event_reg_fd(s, tls_server_reply_cb, sd, "tls server reply") < 0)
        goto done;
    /* Unregister old + register new timeout */
    if (tls_client_timeout(ta) < 0)
        goto done;
    retval = 0;
 done:
    return retval;
}

/*! Out must be set to point to the selected protocol (which may be within in). 
 */
static int
tls_proto_select_cb(SSL *s,
                    unsigned char **out,
                    unsigned char *outlen,
                    const unsigned char *in,
                    unsigned int inlen,
                    void *arg)
{
    clixon_debug(CLIXON_DBG_DEFAULT, "%s", __FUNCTION__);
    return 0;
}

/*! Verify tls auth
 *
 * @see  tlsauth_verify_callback
 * This code needs a "X509 store", see X509_STORE_new()
 * crl_file / crl_dir
 */
static int
tls_auth_verify_callback(int             preverify_ok,
                         X509_STORE_CTX *x509_ctx)
{
    return 1; /* success */
}

static SSL_CTX *
tls_ctx_init(const char *cert_path,
             const char *key_path,
             const char *ca_cert_path)
{
    SSL_CTX *ctx = NULL;

    if ((ctx = SSL_CTX_new(TLS_client_method())) == NULL) {
        clicon_err(OE_SSL, 0, "SSL_CTX_new");
        goto done;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, tls_auth_verify_callback);
    /* get peer certificate 
       nc_client_tls_update_opts */
    if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) != 1) {
        clicon_err(OE_SSL, 0, "SSL_CTX_use_certificate_file");
        goto done;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) != 1) {
        clicon_err(OE_SSL, 0, "SSL_CTX_use_PrivateKey_file");
        goto done;
    }
    if (SSL_CTX_load_verify_locations(ctx, ca_cert_path, NULL) != 1) {
        clicon_err(OE_SSL, 0, "SSL_CTX_load_verify_locations");
        goto done;
    }
    (void)SSL_CTX_set_next_proto_select_cb(ctx, tls_proto_select_cb, NULL);
    return ctx;
 done:
    return NULL;
}

static int
usage(char *argv0)
{
    fprintf(stderr, "usage:%s [options]\n"
            "where options are\n"
            "\t-h \t\tHelp\n"
            "\t-D <level> \tDebug\n"
            "\t-f <file> \tHTTP input file (overrides stdin)\n"
            "\t-F ipv4|ipv6 \tSocket address family(ipv4 default)\n"
            "\t-a <addrstr> \tIP address (eg 1.2.3.4) - mandatory\n"
            "\t-p <port>    \tPort (default %d)\n"
            "\t-c <path> \tcert\n"
            "\t-C <path> \tcacert\n"
            "\t-k <path> \tkey\n"
            "\t-n <nr>   \tQuit after this many incoming connections, 0 means no limit. Default: 1\n"
            "\t-t <sec>  \tTimeout in seconds after each accept, if fired just exit. Default: %ds\n"
            "\t-d <sec>  \tTimeout of data requests on a connection in seconds after each accept, if fired either close or keep idle (see -i). Default: 0s\n"
            "\t-i        \tIdle after receiving last reply. Otherwise close directly after receiving last reply\n"
            "\t-e <nr> \tEvent trace on stdout, 1: terse, 2: full\n"
            ,
            argv0,
            RESTCONF_CH_TLS,
            _accept_timeout_s);
    exit(0);
}

int
main(int    argc,
     char **argv)
{
    int                 retval = -1;
    clicon_handle       h;
    int                 c;
    uint16_t            port = RESTCONF_CH_TLS;
    SSL_CTX            *ctx = NULL;
    int                 ss = -1;
    int                 dbg = 0;
    tls_accept_handle  *ta = NULL;
    char               *input_filename = NULL;
    char               *ca_cert_path = NULL;
    char               *cert_path = NULL;
    char               *key_path = NULL;
    FILE               *fp = stdin; /* base file, stdin, can be overridden with -f */
    struct sockaddr_in6 sin6 = {0,}; // because its larger than sin and sa
    struct sockaddr    *sa = (struct sockaddr *)&sin6;
    size_t              sa_len;
    char               *addr = "127.0.0.1";
    char               *family = "inet:ipv4-address";

    /* In the startup, logs to stderr & debug flag set later */
    clicon_log_init(__FILE__, LOG_INFO, CLICON_LOG_STDERR);
    if ((h = clicon_handle_init()) == NULL)
        goto done;
    while ((c = getopt(argc, argv, UTIL_TLS_OPTS)) != -1)
        switch (c) {
        case 'h':
            usage(argv[0]);
            break;
        case 'D':
            if (sscanf(optarg, "%d", &dbg) != 1)
                usage(argv[0]);
            break;
        case 'f':
            if (optarg == NULL || *optarg == '-')
                usage(argv[0]);
            input_filename = optarg;
            break;
        case 'F':
            family = optarg;
            break;
        case 'a':
            addr = optarg;
            break;
        case 'p':
            if (sscanf(optarg, "%hu", &port) != 1)
                usage(argv[0]);
            break;
        case 'c':
            if (optarg == NULL || *optarg == '-')
                usage(argv[0]);
            cert_path = optarg;
            break;
        case 'C':
            if (optarg == NULL || *optarg == '-')
                usage(argv[0]);
            ca_cert_path = optarg;
            break;
        case 'k':
            if (optarg == NULL || *optarg == '-')
                usage(argv[0]);
            key_path = optarg;
            break;
        case 'n':
            if (optarg == NULL || *optarg == '-')
                usage(argv[0]);
            _accepts = atoi(optarg);
            break;
        case 'i': /* keep open, do not close after first reply */
            _idle = 1;
            break;
        case 't': /* accept timeout */
            if (optarg == NULL || *optarg == '-')
                usage(argv[0]);
            _accept_timeout_s = atoi(optarg);
            break;
        case 'd': /* data timeout */
            if (optarg == NULL || *optarg == '-')
                usage(argv[0]);
            _data_timeout_s = atoi(optarg);
            break;
        case 'e': /* Event trace */
            if (optarg == NULL || *optarg == '-')
                usage(argv[0]);
            _event_trace = atoi(optarg);
            _event_f = stdout;
            break;
        default:
            usage(argv[0]);
            break;
        }
    if (cert_path == NULL || key_path == NULL || ca_cert_path == NULL){
        fprintf(stderr, "-c <cert path> and -k <key path> -C <ca-cert> are mandatory\n");
        usage(argv[0]);
    }
    clixon_debug_init(dbg, NULL);

    if (input_filename){
        if ((_input_file = fopen(input_filename, "r")) == NULL){
            clicon_err(OE_YANG, errno, "open(%s)", input_filename);
            goto done;
        }
    }
    if ((ctx = tls_ctx_init(cert_path, key_path, ca_cert_path)) == NULL)
        goto done;
    if (port == 0){
        fprintf(stderr, "-p <port> is invalid\n");
        usage(argv[0]);
        goto done;
    }
    if (addr == NULL){
        fprintf(stderr, "-a <addr> is NULL\n");
        usage(argv[0]);
        goto done;
    }
    if (clixon_inet2sin(family, addr, port, sa, &sa_len) < 0)
        goto done;
   /* Bind port */
    if (callhome_bind(sa, sa_len, 1, &ss) < 0)
        goto done;
    clixon_debug(CLIXON_DBG_DEFAULT, "callhome_bind %s:%hu", addr, port);
    if ((ta = malloc(sizeof(*ta))) == NULL){
        clicon_err(OE_UNIX, errno, "malloc");
        goto done;
    }
    memset(ta, 0, sizeof(*ta));
    ta->ta_ctx = ctx;
    ta->ta_ss = ss;
    gettimeofday(&ta->ta_t0, NULL);
    if (clixon_event_reg_fd(ss, tls_server_accept_cb, ta, "tls server accept") < 0)
        goto done;
    if (tls_client_timeout(ta) < 0)
        goto done;
    if (clixon_event_loop(h) < 0)
        goto done;
    retval = 0;
 done:
    if (ss != -1)
        clixon_event_unreg_fd(ss, tls_server_accept_cb);
    if (ta)
        free(ta);
    if (fp)
        fclose(fp);
    if (ss != -1)
        close(ss);
    if (ctx)
        SSL_CTX_free(ctx);        /* release context */
    clicon_handle_exit(h); /* frees h and options (and streams) */
    clixon_err_exit();
    clixon_debug(CLIXON_DBG_DEFAULT, "clixon_restconf_callhome_client pid:%u done", getpid());
    clicon_log_exit(); /* Must be after last clixon_debug */
    return retval;
}
