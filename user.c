#include "tintin.h"
#include "protos/user_pipe.h"
#include "protos/user_tty.h"
#include "protos/utils.h"

extern int colors[];
const char *attribs[16]={"",";5",";3",";3;5",";4",";4;5",";4;3",";4;3;5",
        ";9",";5;9",";3;9",";3;5;9",";4;9",";4;5;9",";4;3;9",";4;3;5;9",
};
#ifdef GRAY2
const char *fcolors[16]={";30", ";34", ";32", ";36",
                            ";31", ";35", ";33", "",
                        ";2",  ";34;1",";32;1",";36;1",
                            ";31;1",";35;1",";33;1",";1"};
const char *bcolors[8]={"",   ";44",   ";42",  ";46",
                            ";41", ";45", ";43", "47"};
#define COLORCODE(c) "\033[0%s%s%sm",fcolors[(c)&15],bcolors[((c)>>4)&7],attribs[(c))>>7]
#else
#define COLORCODE(c) "\033[0%s;3%d;4%d%sm",((c)&8)?";1":"",colors[(c)&7],colors[((c)>>4)&7],attribs[(c)>>7]
#endif

char done_input[BUFFER_SIZE];
bool tty;
bool isstatus;

bool ui_sep_input=false;
bool ui_con_buffer=false;
bool ui_keyboard=false;
bool ui_own_output=false;
bool ui_drafts=false;

int LINES=0;
int COLS=0;

void user_setdriver(int dr)
{
    switch (dr)
    {
    case 0:
        userpipe_initdriver();
        break;
    case 1:
        usertty_initdriver();
        break;
    default:
        syserr("No such driver: %d", dr);
    }
}

void user_illegal()
{
    syserr("DRIVER: operation not supported");
}

void user_noop()
{
}
