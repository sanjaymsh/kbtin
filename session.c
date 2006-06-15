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
extern void textout(char *txt);
extern void textout_draft(char *txt);
extern void tintin_puts(char *cptr, struct session *ses);
extern void tintin_puts1(char *cptr, struct session *ses);
extern void tintin_printf(struct session *ses,char *format,...);
extern void tintin_eprintf(struct session *ses,char *format,...);
extern struct hashtable* copy_hash(struct hashtable *h);
extern void do_in_MUD_colors(char *txt,int quotetype);
extern int isatom(char *arg);
extern struct session* do_hook(struct session *ses, int t, char *data, int blockzap);

extern struct session *sessionlist, *activesession, *nullsession;


int session_exists(char *name)
{
    struct session *sesptr;
    
    for (sesptr = sessionlist; sesptr; sesptr = sesptr->next)
        if (!strcmp(sesptr->name, name))
            return 1;
    return 0;
}

void make_name(char *str, char *basis, int run)
{
    char *t;
    int i,j;
    
    if (run)
        for(t=basis; (*t=='/')||isalnum(*t)||(*t=='_'); t++)
            if (*t=='/')
                basis=t+1;    
    if (!isalpha(*basis))
        goto noname;
    strcpy(str, basis);
    for(t=str; isalnum(*t)||(*t=='_'); t++);
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
            strcpy(str, t+1);
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
    char left[BUFFER_SIZE], right[BUFFER_SIZE];
    int sock;
    char *host, *port;

    if (list_sessions(arg,ses,left,right))
        return(ses);	/* (!*left)||(!*right) */

    port = host = space_out(mystrdup(right));

    if (!*host)
    {
        tintin_eprintf(ses,"#session: HEY! SPECIFY AN ADDRESS WILL YOU?");
        return ses;
    }
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
    char left[BUFFER_SIZE], right[BUFFER_SIZE];
    int sock;

    if (list_sessions(arg,ses,left,right))
        return(ses);	/* (!*left)||(!*right) */

    if (!*right)
    {
        tintin_eprintf(ses, "#run: HEY! SPECIFY AN COMMAND, WILL YOU?");
        return(ses);
    };

    if (!(sock=run(right)))
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

    newsession = (struct session *)malloc(sizeof(struct session));

    newsession->name = mystrdup(name);
    newsession->address = mystrdup(address);
    newsession->tickstatus = FALSE;
    newsession->tick_size = ses->tick_size;
    newsession->pretick = ses->pretick;
    newsession->time0 = 0;
    newsession->snoopstatus = 0;
    newsession->logfile = 0;
    newsession->logname = 0;
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
    newsession->ga = 0;
    newsession->gas = 0;
    newsession->server_echo = 0;
    newsession->telnet_buflen = 0;
    newsession->last_term_type = 0;
    newsession->next = sessionlist;
    for (i = 0; i < HISTORY_SIZE; i++)
        newsession->history[i] = NULL;
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
    newsession->debuglogfile=0;
    newsession->debuglogname=0;
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
    sessionlist = newsession;
    activesession = newsession;

    return do_hook(newsession, HOOK_OPEN, 0, 0);
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
    ses->closing=2;
    do_hook(act=ses, HOOK_CLOSE, 0, 1);

    kill_all(ses, END);
    /* printf("DEBUG: Hist: %d \n\r",HISTORY_SIZE); */
    /* CHANGED to fix a possible memory leak
       for(i=0; i<HISTORY_SIZE; i++)
       ses->history[i]=NULL;
     */
    for (i = 0; i < HISTORY_SIZE; i++)
        if ((ses->history[i]))
            free(ses->history[i]);
    if (ses == sessionlist)
        sessionlist = ses->next;
    else
    {
        for (sesptr = sessionlist; sesptr->next != ses; sesptr = sesptr->next) ;
        sesptr->next = ses->next;
    }
    if (ses==activesession)
    {
#ifdef UI_FULLSCREEN
        textout_draft(0);
#endif
        sprintf(buf,"%s\n",ses->last_line);
        do_in_MUD_colors(buf,0);
        textout(buf);
    };
    sprintf(buf, "#SESSION '%s' DIED.", ses->name);
    tintin_puts(buf, NULL);
    /*  if(write(ses->socket, "ctld\n", 5)<5)
         syserr("write in cleanup"); *//* can't do this, cozof the peer stuff in net.c */
    if (close(ses->socket) == -1)
        syserr("close in cleanup");
    if (ses->logfile)
        fclose(ses->logfile);
    if (ses->debuglogfile)
        fclose(ses->debuglogfile);
    for(i=0;i<NHOOKS;i++)
    	free(ses->hooks[i]);

    free(ses);
}

void seslist(char *result)
{
    struct session *sesptr;
    int flag=0;

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
                    result+=sprintf(result,"%s",sesptr->name);
                else
                    result+=sprintf(result,"{%s}",sesptr->name);
                /* FIXME: check for buffer overflows.  Possible only
                          for patological session names, but still...  */
            }
    }
}
