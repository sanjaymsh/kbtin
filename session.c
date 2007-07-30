/* $Id: session.c,v 2.3 1998/10/12 09:45:23 jku Exp $ */
/* Autoconf patching by David Hedbor, neotron@lysator.liu.se */
/*********************************************************************/
/* file: session.c.c - funtions related to sessions                  */
/*                             TINTIN III                            */
/*          (T)he K(I)cki(N) (T)ickin D(I)kumud Clie(N)t             */
/*                     coded by peter unold 1992                     */
/*********************************************************************/
#include "config.h"
#include <ctype.h>
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#include "tintin.h"
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#include "ui.h"

void show_session(struct session *ses);
struct session *new_session(char *name,char *address,int sock,int issocket,struct session *ses);

extern char *get_arg_in_braces(char *s,char *arg,int flag);
extern char *space_out(char *s);
extern char *mystrdup(char *s);
extern struct listnode *copy_list(struct listnode *sourcelist,int mode);
extern struct listnode *init_list(void);
extern int run(char *command);
extern void copyroutes(struct session *ses1,struct session *ses2);
extern int connect_mud(char *host, char *port, struct session *ses);
extern void kill_all(struct session *ses, int mode);
extern void prompt(struct session *ses);
extern void syserr(char *msg, ...);
extern void tintin_puts(char *cptr, struct session *ses);
extern void tintin_puts1(char *cptr, struct session *ses);
extern void tintin_printf(struct session *ses,char *format,...);
extern void tintin_eprintf(struct session *ses,char *format,...);
extern struct hashtable* copy_hash(struct hashtable *h);
extern void do_in_MUD_colors(char *txt,int quotetype);
extern int isatom(char *arg);
extern struct session* do_hook(struct session *ses, int t, char *data, int blockzap);
extern void utf8_to_local(char *d, char *s);
extern void nullify_conv(struct charset_conv *conv);
extern void cleanup_conv(struct charset_conv *conv);
extern int new_conv(struct charset_conv *conv, char *name, int dir);
extern void convert(struct charset_conv *conv, char *outbuf, char *inbuf, int dir);
extern void log_off(struct session *ses);

extern struct session *sessionlist, *activesession, *nullsession;
extern char *history[HISTORY_SIZE];
extern char *user_charset_name;
extern int any_closed;


int session_exists(char *name)
{
    struct session *sesptr;
    
    for (sesptr = sessionlist; sesptr; sesptr = sesptr->next)
        if (!strcmp(sesptr->name, name))
            return 1;
    return 0;
}

#define is7alpha(x) ((((x)>='A')&&((x)<='Z')) || (((x)>='a')&&((x)<='z')))
#define is7alnum(x) ((((x)>='0')&&((x)<='9')) || is7alpha(x))
/* FIXME: use non-ascii letters in generated names */

/* NOTE: basis is in the local charset, not UTF-8 */
void make_name(char *str, char *basis, int run)
{
    char *t;
    int i,j;
    
    if (run)
        for(t=basis; (*t=='/')||is7alnum(*t)||(*t=='_'); t++)
            if (*t=='/')
                basis=t+1;    
    if (!is7alpha(*basis))
        goto noname;
    strcpy(str, basis);
    for(t=str; is7alnum(*t)||(*t=='_'); t++);
    *t=0;
    if (!session_exists(str))
        return;
    i=2;
    do sprintf(t, "%d", i++); while (session_exists(str));
    return;
noname:
    for(i=1; ; i++)
    {
        j=i;
        *(t=str+10)=0;
        do
        {
            *--t='a'+(j-1)%('z'+1-'a');
            j=(j-1)/('z'+1-'a');
        } while (j);
        if (!session_exists(t))
        {
            strcpy(str, t);
            return;
        }
    }
}


/*
  common code for #session and #run for cases of:
    #session            - list all sessions
    #session {a}        - print info about session a
  (opposed to #session {a} {mud.address.here 666} - starting a new session)
*/
int list_sessions(char *arg,struct session *ses,char *left,char *right)
{
    struct session *sesptr;
    arg = get_arg_in_braces(arg, left, 0);
    arg = get_arg_in_braces(arg, right, 1);

    if (!*left)
    {
        tintin_puts("#THESE SESSIONS HAS BEEN DEFINED:", ses);
        for (sesptr = sessionlist; sesptr; sesptr = sesptr->next)
            if (sesptr!=nullsession)
                show_session(sesptr);
        prompt(ses);
    }
    else if (*left && !*right)
    {
        for (sesptr = sessionlist; sesptr; sesptr = sesptr->next)
            if (!strcmp(sesptr->name, left))
            {
                show_session(sesptr);
                break;
            }
        if (sesptr == NULL)
        {
            tintin_puts("#THAT SESSION IS NOT DEFINED.", ses);
            prompt(NULL);
        }
    }
    else
    {
        if (session_exists(left))
        {
            tintin_eprintf(ses,"#THERE'S A SESSION WITH THAT NAME ALREADY.");
            prompt(NULL);
            return 1;
        };
        return 0;
    };
    return 1;
}

/************************/
/* the #session command */
/************************/
struct session *session_command(char *arg,struct session *ses)
{
    char left[BUFFER_SIZE], right[BUFFER_SIZE], host[BUFFER_SIZE];
    int sock;
    char *port;

    if (list_sessions(arg,ses,left,right))
        return(ses);	/* (!*left)||(!*right) */

    strcpy(host, space_out(right));

    if (!*host)
    {
        tintin_eprintf(ses,"#session: HEY! SPECIFY AN ADDRESS WILL YOU?");
        return ses;
    }
    
    port=host;
    while (*port && !isspace(*port))
        port++;
    if (*port)
    {
        *port++ = '\0';
        port = space_out(port);
    }

    if (!*port)
    {
        tintin_eprintf(ses, "#session: HEY! SPECIFY A PORT NUMBER WILL YOU?");
        return ses;
    }
    if (!(sock = connect_mud(host, port, ses)))
        return ses;

    return(new_session(left,right,sock,1,ses));
}


/********************/
/* the #run command */
/********************/
struct session *run_command(char *arg,struct session *ses)
{
    char left[BUFFER_SIZE], right[BUFFER_SIZE], ustr[BUFFER_SIZE];
    int sock;

    if (list_sessions(arg,ses,left,right))
        return(ses);	/* (!*left)||(!*right) */

    if (!*right)
    {
        tintin_eprintf(ses, "#run: HEY! SPECIFY AN COMMAND, WILL YOU?");
        return(ses);
    };

#ifdef UTF8
    utf8_to_local(ustr, right);
    if (!(sock=run(ustr)))
#else
    if (!(sock=run(right)))
#endif
    {
        tintin_eprintf(ses, "#forkpty() FAILED!");
        return ses;
    }

    return(new_session(left,right,sock,0,ses));
}


/******************/
/* show a session */
/******************/
void show_session(struct session *ses)
{
    char temp[BUFFER_SIZE];

    sprintf(temp, "%-10s{%s}", ses->name, ses->address);

    if (ses == activesession)
        strcat(temp, " (active)");
    if (ses->snoopstatus)
        strcat(temp, " (snooped)");
    if (ses->logfile)
        strcat(temp, " (logging)");
    tintin_printf(0, "%s", temp);
}

/**********************************/
/* find a new session to activate */
/**********************************/
struct session *newactive_session(void)
{
    if ((activesession=sessionlist)==nullsession)
        activesession=activesession->next;
    if (activesession)
    {
        char buf[BUFFER_SIZE];

        sprintf(buf, "#SESSION '%s' ACTIVATED.", activesession->name);
        tintin_puts1(buf, activesession);
        return do_hook(activesession, HOOK_ACTIVATE, 0, 0);
    }
    else
    {
        activesession=nullsession;
        tintin_puts1("#THERE'S NO ACTIVE SESSION NOW.", activesession);
        return do_hook(activesession, HOOK_ACTIVATE, 0, 0);
    }
}


/**********************/
/* open a new session */
/**********************/
struct session *new_session(char *name,char *address,int sock,int issocket,struct session *ses)
{
    struct session *newsession;
    int i;

    newsession = TALLOC(struct session);

    newsession->name = mystrdup(name);
    newsession->address = mystrdup(address);
    newsession->tickstatus = FALSE;
    newsession->tick_size = ses->tick_size;
    newsession->pretick = ses->pretick;
    newsession->time0 = 0;
    newsession->snoopstatus = 0;
    newsession->logfile = 0;
    newsession->logname = 0;
    newsession->logtype = ses->logtype;
    newsession->ignore = ses->ignore;
    newsession->aliases = copy_hash(ses->aliases);
    newsession->actions = copy_list(ses->actions, PRIORITY);
    newsession->prompts = copy_list(ses->prompts, PRIORITY);
    newsession->subs = copy_list(ses->subs, ALPHA);
    newsession->myvars = copy_hash(ses->myvars);
    newsession->highs = copy_list(ses->highs, ALPHA);
    newsession->pathdirs = copy_hash(ses->pathdirs);
    newsession->socket = sock;
    newsession->antisubs = copy_list(ses->antisubs, ALPHA);
    newsession->binds = copy_hash(ses->binds);
    newsession->issocket = issocket;
    newsession->naws = !issocket;
#ifdef HAVE_LIBZ
    newsession->can_mccp = 0;
    newsession->mccp = 0;
    newsession->mccp_more = 0;
#endif
    newsession->ga = 0;
    newsession->gas = 0;
    newsession->server_echo = 0;
    newsession->telnet_buflen = 0;
    newsession->last_term_type = 0;
    newsession->next = sessionlist;
    newsession->path = init_list();
    newsession->no_return = 0;
    newsession->path_length = 0;
    newsession->more_coming = 0;
    newsession->events = NULL;
    newsession->verbose = ses->verbose;
    newsession->blank = ses->blank;
    newsession->echo = ses->echo;
    newsession->speedwalk = ses->speedwalk;
    newsession->togglesubs = ses->togglesubs;
    newsession->presub = ses->presub;
    newsession->verbatim = ses->verbatim;
    newsession->sessionstart=newsession->idle_since=time(0);
    newsession->nagle=0;
    newsession->halfcr_in=0;
    newsession->halfcr_log=0;
    newsession->debuglogfile=0;
    newsession->debuglogname=0;
    newsession->partial_line_marker = mystrdup(ses->partial_line_marker);
    memcpy(newsession->mesvar, ses->mesvar, sizeof(int)*(MAX_MESVAR+1));
    for (i=0;i<MAX_LOCATIONS;i++)
    {
        newsession->routes[i]=0;
        newsession->locations[i]=0;
    };
    copyroutes(ses,newsession);
    newsession->last_line[0]=0;
    for (i=0;i<NHOOKS;i++)
        if(ses->hooks[i])
            newsession->hooks[i]=mystrdup(ses->hooks[i]);
        else
            newsession->hooks[i]=0;
    newsession->closing=0;
#ifdef UTF8
    newsession->charset = mystrdup(issocket ? ses->charset : user_charset_name);
    newsession->logcharset = logcs_is_special(ses->logcharset) ?
                              ses->logcharset : mystrdup(ses->logcharset);
    if (!new_conv(&newsession->c_io, newsession->charset, 0))
        tintin_eprintf(0, "#Warning: can't open charset: %s", newsession->charset);
    nullify_conv(&newsession->c_log);
#endif
    sessionlist = newsession;
    activesession = newsession;

    return do_hook(newsession, HOOK_OPEN, 0, 0);
}

/***************************************************************************************/
/* look for the session on the list.  If it's there, return a pointer to its reference */
/***************************************************************************************/
struct session **is_alive(struct session *ses)
{
    struct session *sesptr;
    
    if (ses==sessionlist)
        return &sessionlist;
    for(sesptr = sessionlist; sesptr && sesptr->next!=ses; sesptr=sesptr->next) ;
    if (sesptr)
        return &sesptr->next;
    return 0;
}

/*****************************************************************************/
/* cleanup after session died. if session=activesession, try find new active */
/*****************************************************************************/
void cleanup_session(struct session *ses)
{
    int i;
    char buf[BUFFER_SIZE];
    struct session *sesptr, *act;
    
    if (ses->closing)
    	return;
    any_closed=1;
    ses->closing=2;
    do_hook(act=ses, HOOK_CLOSE, 0, 1);

    kill_all(ses, END);
    if (ses == sessionlist)
        sessionlist = ses->next;
    else
    {
        for (sesptr = sessionlist; sesptr->next != ses; sesptr = sesptr->next) ;
        sesptr->next = ses->next;
    }
    if (ses==activesession)
    {
        user_textout_draft(0, 0);
        sprintf(buf,"%s\n",ses->last_line);
#ifdef UTF8
        convert(&ses->c_io, ses->last_line, buf, -1);
        do_in_MUD_colors(ses->last_line,0);
        user_textout(ses->last_line);
#else
        do_in_MUD_colors(buf,0);
        user_textout(buf);
#endif
    };
    sprintf(buf, "#SESSION '%s' DIED.", ses->name);
    tintin_puts(buf, NULL);
    if (close(ses->socket) == -1)
        syserr("close in cleanup");
    if (ses->logfile)
        log_off(ses);
    if (ses->debuglogfile)
        fclose(ses->debuglogfile);
    for(i=0;i<NHOOKS;i++)
    	SFREE(ses->hooks[i]);
    SFREE(ses->name);
    SFREE(ses->address);
    SFREE(ses->partial_line_marker);
#ifdef UTF8
    cleanup_conv(&ses->c_io);
    SFREE(ses->charset);
    if (!logcs_is_special(ses->logcharset))
        SFREE(ses->logcharset);
#endif
#ifdef HAVE_LIBZ
    if (ses->mccp)
    {
        inflateEnd(ses->mccp);
        TFREE(ses->mccp, z_stream);
    }
#endif
    
    TFREE(ses, struct session);
}

void seslist(char *result)
{
    struct session *sesptr;
    int flag=0;
    char *r0=result;

    if ((sessionlist!=nullsession)||(nullsession->next))
    {
        for (sesptr = sessionlist; sesptr; sesptr = sesptr->next)
            if (sesptr!=nullsession)
            {
                if (flag)
                    *result++=' ';
                else
                    flag=1;
                if (isatom(sesptr->name))
                    result+=snprintf(result, BUFFER_SIZE-5+r0-result,
                        "%s",sesptr->name);
                else
                    result+=snprintf(result, BUFFER_SIZE-5+r0-result,
                        "{%s}",sesptr->name);
                if (result-r0>BUFFER_SIZE-10)
                    return; /* pathological session names */
            }
    }
}
