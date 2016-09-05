/*********************************************************************/
/* file: history.c - functions for the history stuff                 */
/*                             TINTIN III                            */
/*          (T)he K(I)cki(N) (T)ickin D(I)kumud Clie(N)t             */
/*                     coded by peter unold 1992                     */
/*********************************************************************/
#include "tintin.h"
#include "protos/globals.h"
#include "protos/print.h"
#include "protos/parse.h"
#include "protos/utils.h"

static void insert_history(const char *buffer);


/************************/
/* the #history command */
/************************/
void history_command(const char *arg, struct session *ses)
{
    for (int i = HISTORY_SIZE - 1; i >= 0; i--)
        if (history[i])
            tintin_printf(ses, "%2d %s ", i, history[i]);
}



void do_history(char *buffer, struct session *ses)
{
    char result[BUFFER_SIZE];
    const char *cptr;

    if (!ses->verbatim && *(cptr=space_out(buffer)))
    {

        if (*cptr == '!')
        {
            if (*(cptr + 1) == '!')
            {
                if (history[0])
                {
                    strcpy(result, history[0]);
                    strcat(result, cptr + 2);
                    strcpy(buffer, result);
                }
            }
            else if (isadigit(*(cptr + 1)))
            {
                int i = atoi(cptr + 1);

                if (i >= 0 && i < HISTORY_SIZE && history[i])
                {
                    strcpy(result, history[i]);
                    strcat(result, cptr + 2);
                    strcpy(buffer, result);
                }
            }
            else
            {
                for (int i = 0; i < HISTORY_SIZE && history[i]; i++)
                    if (is_abrev(cptr + 1, history[i]))
                    {
                        strcpy(buffer, history[i]);
                        break;
                    }
            }

        }
    }
    insert_history(buffer);
}

/***********************************************/
/* insert buffer into a session`s history list */
/***********************************************/
static void insert_history(const char *buffer)
{
    if (history[HISTORY_SIZE - 1])
        SFREE(history[HISTORY_SIZE - 1]);

    for (int i = HISTORY_SIZE - 1; i > 0; i--)
        history[i] = history[i - 1];

    history[0] = mystrdup(buffer);
}

#if 0
/************************************************************/
/* do all the parse stuff for !XXXX history commands        */
/************************************************************/
struct session* parse_history(const char *command, const char *arg, struct session *ses)
{
    if ((*(command + 1) == '!' || !*(command + 1)) && history[0])
        return parse_input(history[0], true, ses); /* we're already not in verbatim */

    else if (isadigit(*(command + 1)))
    {
        int i = atoi(command + 1);

        if (i >= 0 && i < HISTORY_SIZE && history[i])
        {
            return parse_input(history[i], true, ses);
        }
    }
    tintin_eprintf(ses, "#HISTORY: I DON'T REMEMBER THAT COMMAND.");

    return ses;
}
#endif
