/*********************************************************************/
/* file: net.c - do all the net stuff                                */
/*                             TINTIN III                            */
/*          (T)he K(I)cki(N) (T)ickin D(I)kumud Clie(N)t             */
/*                     coded by peter unold 1992                     */
/*********************************************************************/
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include "tintin.h"
#include "protos/files.h"
#include "protos/globals.h"
#include "protos/hooks.h"
#include "protos/print.h"
#include "protos/prof.h"
#include "protos/run.h"
#include "protos/telnet.h"
#include "protos/unicode.h"
#include "protos/utils.h"

#ifndef BADSIG
#define BADSIG (void (*)(int))-1
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#include <arpa/inet.h>
#endif
#include <assert.h>


static void alarm_func(int);

#ifdef HAVE_ZLIB
static int init_mccp(struct session *ses, int cplen, const char *cpsrc);
#endif

static bool abort_connect;

#ifdef HAVE_GETADDRINFO
static const char* afstr(int af)
{
    static char msg[19];

    switch (af)
    {
        case AF_INET:
            return "IPv4";
        case AF_INET6:
            return "IPv6";
        default:
            /* No symbolic names for AF_UNIX, AF_IPX and the like.
             * We would need separate autoconf checks just for them,
             * and they don't support TCP anyway.  We probably should
             * allow all SOCK_STREAM-capable transports, but I'm a bit
             * unsure.  */
            snprintf(msg, 19, "AF=%d", af);
            return msg;
    }
}


/**************************************************/
/* try connect to the mud specified by the args   */
/* return fd on success / 0 on failure            */
/**************************************************/
int connect_mud(const char *host, const char *port, struct session *ses)
{
    int err, val;
    struct addrinfo *ai, hints;
    int sock;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_protocol=IPPROTO_TCP;
    hints.ai_flags=AI_ADDRCONFIG;

    if ((err=getaddrinfo(host, port, &hints, &ai)))
    {
        if (err==-2)
            tintin_eprintf(ses, "#Unknown host: {%s}", host);
        else
            tintin_eprintf(ses, "#ERROR: %s", gai_strerror(err));
        return 0;
    }

    if (signal(SIGALRM, alarm_func) == BADSIG)
        syserr("signal SIGALRM");

    for (struct addrinfo *addr=ai; addr; addr=addr->ai_next)
    {
        tintin_printf(ses, "#Trying to connect... (%s) (charset=%s)",
            afstr(addr->ai_family), ses->charset);

        if ((sock=socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol))==-1)
        {
            tintin_eprintf(ses, "#ERROR: %s", strerror(errno));
            continue;
        }

        val=IPTOS_LOWDELAY;
        if (setsockopt(sock, IPPROTO_IP, IP_TOS, &val, sizeof(val)))
        {
            /*tintin_eprintf(ses, "#setsockopt: %s", strerror(errno))*/
            /* FIXME: BSD doesn't like this on IPv6 */
        }

        val=1;
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
#ifdef TCP_KEEPIDLE
        val=30; /* in seconds */
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val));
#endif

        abort_connect=false;
        alarm(15);
    intr:
        if ((connect(sock, addr->ai_addr, addr->ai_addrlen)))
        {
            switch (errno)
            {
            case EINTR:
                if (abort_connect)
                {
                    tintin_eprintf(ses, "#CONNECTION TIMED OUT");
                    continue;
                }
                else
                    goto intr;
            default:
                alarm(0);
                tintin_eprintf(ses, "#%s", strerror(errno));
                continue;
            }
        }

        alarm(0);
        freeaddrinfo(ai);
        return sock;
    }

    if (!ai)
        tintin_eprintf(ses, "#No valid addresses for {%s}", host);
    freeaddrinfo(ai);
    return 0;
}
#else
int connect_mud(const char *host, const char *port, struct session *ses)
{
    int sock, val;
    struct sockaddr_in sockaddr;

    if (isadigit(*host))         /* interpret host part */
        sockaddr.sin_addr.s_addr = inet_addr(host);
    else
    {
        struct hostent *hp;

        if (!(hp = gethostbyname(host)))
        {
            tintin_eprintf(ses, "#ERROR - UNKNOWN HOST: {%s}", host);
            return 0;
        }
        memcpy((char *)&sockaddr.sin_addr, hp->h_addr, sizeof(sockaddr.sin_addr));
    }

    if (isadigit(*port))
        sockaddr.sin_port = htons(atoi(port));  /* intepret port part */
    else
    {
        tintin_eprintf(ses, "#THE PORT SHOULD BE A NUMBER (got {%s}).", port);
        return 0;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        syserr("socket");

    sockaddr.sin_family = AF_INET;

    val=IPTOS_LOWDELAY;
    setsockopt(sock, IPPROTO_IP, IP_TOS, &val, sizeof(val));

    tintin_printf(ses, "#Trying to connect...");

    if (signal(SIGALRM, alarm_func) == BADSIG)
        syserr("signal SIGALRM");

    alarm(15);                  /* We'll allow connect to hang in 15seconds! NO MORE! */
    val = connect(sock, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    alarm(0);

    if (val)
    {
        close(sock);
        switch (errno)
        {
        case EINTR:
            tintin_eprintf(ses, "#CONNECTION TIMED OUT.");
            break;
        case ECONNREFUSED:
            tintin_eprintf(ses, "#ERROR - Connection refused.");
            break;
        case ENETUNREACH:
            tintin_eprintf(ses, "#ERROR - Network unreachable.");
            break;
        default:
            tintin_eprintf(ses, "#Couldn't connect to %s:%s", host, port);
        }
        return 0;
    }
    return sock;
}
#endif

/*****************/
/* alarm handler */
/*****************/
static void alarm_func(int k)
{
    abort_connect=true;
}

/********************************************************************/
/* write line to the mud ses is connected to - add \n or \r\n first */
/********************************************************************/
void write_line_mud(const char *line, struct session *ses)
{
    char rstr[BUFFER_SIZE];
    PROFPUSH("conv: utf8->remote");
    convert(&ses->c_io, rstr, line, 1);
    PROFPOP;

    if (*line)
        ses->idle_since=time(0);
    if (ses->issocket)
    {
        if (!ses->nagle)
        {
            setsockopt(ses->socket, IPPROTO_TCP, TCP_NODELAY, &ses->nagle,
                sizeof(ses->nagle));
            ses->nagle=1;
        }
        telnet_write_line(rstr, ses, true);
    }
    else if (ses==nullsession)
        tintin_eprintf(ses, "#spurious output: %s", line);  /* CHANGE ME */
    else
        pty_write_line(rstr, ses->socket);
    do_hook(ses, HOOK_SEND, line, true);
}

/******************************************/
/* write control chars, without a newline */
/******************************************/
void write_raw_mud(const char *line, int len, struct session *ses)
{
    const char *lp=line;
    char *rp, rstr[BUFFER_SIZE];
    int ret;

    /* not updating $IDLETIME, it's most likely not a command */
    if (ses->issocket && !ses->nagle)
    {
        setsockopt(ses->socket, IPPROTO_TCP, TCP_NODELAY, &ses->nagle,
            sizeof(ses->nagle));
        ses->nagle=1;
    }

    assert(line[len]==0);
    /* convert a string with zero bytes, guaranteed zero past len */
    while (lp < line+len)
    {
        if (*lp)
        {
            convert(&ses->c_io, rstr, lp, 1);
            while (*lp)
                lp++;
            rp=rstr;
            while (*rp)
                rp++;

            if (ses->issocket)
                *rp=ret=0, telnet_write_line(rstr, ses, false);
            else
                ret=write(ses->socket, rstr, rp-rstr);
        }
        else
        {
            lp++;
            ret=write(ses->socket, "", 1);
        }
        if (ret<0)
            tintin_eprintf(ses, "#error writing to session: %s", strerror(errno));
    }
}

void flush_socket(struct session *ses)
{
    setsockopt(ses->socket, IPPROTO_TCP, TCP_NODELAY, &ses->nagle,
        sizeof(ses->nagle));
    ses->nagle=0;
}

/***************************************************/
/* low-level read/write, includes encryption layer */
/***************************************************/
static int read_socket(struct session *ses, char *buffer, int len)
{
#ifdef HAVE_GNUTLS
    int ret;

    if (ses->ssl)
    {
        do
        {
            ret=gnutls_record_recv(ses->ssl, buffer, len);
        } while (ret==GNUTLS_E_INTERRUPTED || ret==GNUTLS_E_AGAIN);
        return ret;
    }
    else
#endif
        return read(ses->socket, buffer, len);
}

int write_socket(struct session *ses, char *buffer, int len)
{
#ifdef HAVE_GNUTLS
    int ret;

    if (ses->ssl)
    {
        ret=gnutls_record_send(ses->ssl, buffer, len);
        while (ret==GNUTLS_E_INTERRUPTED || ret==GNUTLS_E_AGAIN)
            ret=gnutls_record_send(ses->ssl, 0, 0);
        return ret;
    }
    else
#endif
        return write(ses->socket, buffer, len);
}

/***********************************************************************/
/* read at most BUFFER_SIZE chars from mud - do compression and telnet */
/***********************************************************************/
int read_buffer_mud(char *buffer, struct session *ses)
{
    int i, didget, b;
    char *cpsource, *cpdest;
#define tmpbuf ses->telnet_buf
#define len ses->telnet_buflen

    if (!ses->issocket)
    {
        didget=read(ses->socket, buffer, INPUT_CHUNK);
        if (didget<=0)
            return -1;
        ses->server_idle_since=time(0);
        ses->more_coming=(didget==INPUT_CHUNK);
        buffer[didget]=0;
        return didget;
    }

#ifdef HAVE_ZLIB
    if (ses->mccp)
    {
        if (!ses->mccp_more)
        {
            didget = read_socket(ses, ses->mccp_buf, INPUT_CHUNK);
            if (didget<=0)
            {
                ses->mccp_more=false;
                return -1;
            }
            ses->mccp->next_in = (Bytef*)ses->mccp_buf;
            ses->mccp->avail_in = didget;
        }
        ses->mccp->next_out = (Bytef*)(tmpbuf+len);
        ses->mccp->avail_out = INPUT_CHUNK-len;
        switch (i=inflate(ses->mccp, Z_SYNC_FLUSH))
        {
        case Z_OK:
            didget=INPUT_CHUNK-len-ses->mccp->avail_out;
            ses->mccp_more = !ses->mccp->avail_out;
            break;
        case Z_STREAM_END:
#ifdef TELNET_DEBUG
            tintin_printf(ses, "#COMPRESSION END, DISABLING MCCP.");
#endif
            didget=INPUT_CHUNK-len-ses->mccp->avail_out;
            inflateEnd(ses->mccp);
            TFREE(ses->mccp, z_stream);
            ses->mccp=0;
            break;
        case Z_BUF_ERROR:
            didget=0;
            ses->mccp_more=0;
            break;
        default:
            debuglog(ses, "COMPRESSION ERROR: %d", i);
            tintin_eprintf(ses, "#COMPRESSION ERROR");
            ses->mccp_more=0;
            return -1;
        }
    }
    else
#endif
    {
        didget = read_socket(ses, tmpbuf+len, INPUT_CHUNK-len);

        if (didget < 0)
            return -1;

        else if (didget == 0)
            return -1;
    }

    *(tmpbuf+len+didget)=0;
#if 0
    tintin_printf(ses, "~8~text:[%s]~-1~", tmpbuf);
#endif

    ses->server_idle_since=time(0);
    ses->more_coming = (didget+=len) == INPUT_CHUNK;
    len=0;
    ses->ga=false;

    tmpbuf[didget]=0;
    cpsource = tmpbuf;
    cpdest = buffer;
    i = didget;
    while (i > 0)
    {
        switch (*(const unsigned char *)cpsource)
        {
        case 0:
            i--;
            didget--;
            cpsource++;
            break;
        case 255:
            b=do_telnet_protocol(cpsource, i, ses);
            switch (b)
            {
            case -1:
                len=i;
                memmove(tmpbuf, cpsource, i);
                *cpdest=0;
                return didget-len;
            case -2:
                i-=2;
                didget-=2;
                cpsource+=2;
                if (!i)
                    ses->ga=true;
                break;
            case -3:
                i -= 2;
                didget-=1;
                *cpdest++=(char)255;
                cpsource+=2;
                break;
#ifdef HAVE_ZLIB
            case -4:
                didget-=i;
                i-=5;
                cpsource+=5;
                if (init_mccp(ses, i, cpsource)<0)
                    return -1;
                *cpdest = 0;
                return didget;
                break;
#endif
            default:
                i -= b;
                didget-=b;
                cpsource += b;
            }
            break;
        default:
            *cpdest++ = *cpsource++;
            i--;
        }
    }
    *cpdest = '\0';
    return didget;
}

#ifdef HAVE_ZLIB
static int init_mccp(struct session *ses, int cplen, const char *cpsrc)
{
    if (ses->mccp)
        return 0;

    ses->mccp = TALLOC(z_stream);

    ses->mccp->data_type = Z_ASCII;
    ses->mccp->zalloc    = 0;
    ses->mccp->zfree     = 0;
    ses->mccp->opaque    = NULL;

    if (inflateInit(ses->mccp) != Z_OK)
    {
        tintin_eprintf(ses, "#FAILED TO INITIALIZE MCCP2.");
        /* Unrecoverable */
        TFREE(ses->mccp, z_stream);
        ses->mccp = NULL;
        return -1;
    }
#ifdef TELNET_DEBUG
    else
        tintin_printf(ses, "#MCCP2 INITIALIZED.");
#endif
    memcpy(ses->mccp_buf, cpsrc, cplen);
    ses->mccp->next_in = (Bytef*)ses->mccp_buf;
    ses->mccp->avail_in = cplen;
    ses->mccp_more = cplen;
    return 1;
}
#endif
