#include "tintin.h"
#include "protos/action.h"
#include "protos/colors.h"
#include "protos/glob.h"
#include "protos/globals.h"
#include "protos/llist.h"
#include "protos/print.h"
#include "protos/parse.h"
#include "protos/utils.h"
#include "protos/variables.h"


static struct colordef
{
    int num;
    const char *name;
} cNames[]=
    {
        { 0, "black"},
        { 1, "blue"},
        { 2, "green"},
        { 3, "cyan"},
        { 4, "red"},
        { 5, "magenta"},
        { 5, "violet"},
        { 6, "brown"},
        { 7, "grey"},
        { 7, "gray"},
        { 7, "normal"},
        { 8, "faint"},
        { 8, "charcoal"},
        { 8, "dark"},
        { 9, "light blue"},
        {10, "light green"},
        {11, "light cyan"},
        {11, "aqua"},
        {12, "light red"},
        {13, "light magenta"},
        {13, "pink"},
        {14, "yellow"},
        {15, "white"},
        {15, "bold"},
        {112, "inverse"},
        {112, "reverse"},
        {135, "blink"},
        {519, "underline"},
        {519, "underlined"},
        {263, "italic"},
        {-1, ""},
    };

static int highpattern[64];
static int nhighpattern;

static int highcolor; /* an ugly kludge... */
static int get_high_num(const char *hig)
{
    char tmp[BUFFER_SIZE];

    if (!*hig)
        return -1;
    if (isadigit(*hig))
    {
        const char *sl=strchr(hig, '/');
        if (!sl)
            sl=strchr(hig, 0);
        sprintf(tmp, "~%.*s~", (int)(sl-hig), hig);
        sl=tmp;
        if (getcolor(&sl, &highcolor, 0))
            return highcolor;
    }
    for (int code=0;cNames[code].num!=-1;code++)
        if (is_abrev(hig, cNames[code].name))
            return highcolor=cNames[code].num;
    return -1;
}

static bool get_high(const char *hig)
{
    nhighpattern=0;
    if (!*hig)
        return false;
    while (hig&&*hig)
    {
        highcolor=7;
        if ((highpattern[nhighpattern++]=get_high_num(hig))==-1)
            return false;
        if ((hig=strchr(hig, '/')))
            hig++;
        if (nhighpattern==64)
            return true;
    }
    return true;
}

/***************************/
/* the #highlight command  */
/***************************/
void highlight_command(const char *arg, struct session *ses)
{
    char left[BUFFER_SIZE], right[BUFFER_SIZE];
    struct listnode *myhighs, *ln;
    bool colflag = true;
    char *pright, *tmp1, *tmp2, tmp3[BUFFER_SIZE];

    pright = right;
    *pright = '\0';
    myhighs = ses->highs;
    arg = get_arg_in_braces(arg, left, 0);
    arg = get_arg_in_braces(arg, right, 1);
    substitute_vars(left, tmp3);
    substitute_myvars(tmp3, left, ses);
    substitute_vars(right, tmp3);
    substitute_myvars(tmp3, right, ses);
    if (!*left)
    {
        tintin_printf(ses, "#THESE HIGHLIGHTS HAVE BEEN DEFINED:");
        show_list(myhighs);
    }
    else
    {
        tmp1 = left;
        tmp2 = tmp1;
        while (*tmp2 != '\0')
        {
            tmp2++;
            while (*tmp2 != ',' && *tmp2 != '\0')
                tmp2++;
            while (isaspace(*tmp1))
                tmp1++;
            memcpy(tmp3, tmp1, tmp2 - tmp1);
            tmp3[tmp2 - tmp1] = '\0';
            colflag = get_high(tmp3);
            tmp1 = tmp2 + 1;
        }
        if (colflag)
        {
            if (!*right)
            {
                if (ses->mesvar[MSG_HIGHLIGHT] || ses->mesvar[MSG_ERROR])
                    tintin_eprintf(ses, "#Highlight WHAT?");
                return;
            }
            if ((ln = searchnode_list(myhighs, right)))
                deletenode_list(myhighs, ln);
            insertnode_list(myhighs, right, left, get_fastener(right, tmp1), LENGTH);
            hinum++;
            if (ses->mesvar[MSG_HIGHLIGHT])
                tintin_printf(ses, "#Ok. {%s} is now highlighted %s.", right, left);
        }
        else
        {
            if (!puts_echoing && ses->mesvar[MSG_ERROR])
            {
                tintin_eprintf(ses, "#Invalid highlighting color: {%s}", left);
                return;
            }

            if (strcmp(left, "list"))
                tintin_printf(ses, "#Invalid highlighting color, valid colors are:");
            tmp3[0]=0;
            tmp1=tmp3;
            for (int i=0;cNames[i].num!=-1;i++)
            {
                sprintf(left, "%s~7~, ", cNames[i].name);
                if (cNames[i].num)
                    tmp1+=sprintf(tmp1, "~%i~%-20s ", cNames[i].num, left);
                else
                    tmp1+=sprintf(tmp1, "~7~%-20s ", left);
                if ((i%4)==3)
                {
                    tintin_printf(ses, "%s", tmp3);
                    tmp3[0]=0;
                    tmp1=tmp3;
                }
            }
            strcpy(tmp1, "or 0..15:0..7:0..1");
            tintin_printf(ses, "%s", tmp3);
        }


    }
}

/*****************************/
/* the #unhighlight command */
/*****************************/

void unhighlight_command(const char *arg, struct session *ses)
{
    char left[BUFFER_SIZE], result[BUFFER_SIZE];
    struct listnode *myhighs, *ln, *temp;
    bool flag = false;

    myhighs = ses->highs;
    temp = myhighs;
    arg = get_arg_in_braces(arg, left, 1);
    substitute_vars(left, result);
    substitute_myvars(result, left, ses);
    while ((ln = search_node_with_wild(temp, left)))
    {
        if (ses->mesvar[MSG_HIGHLIGHT])
            tintin_printf(ses, "Ok. {%s} is no longer %s.", ln->left, ln->right);
        deletenode_list(myhighs, ln);
        flag = true;
        /*temp = ln;*/
    }
    if (!flag && ses->mesvar[MSG_HIGHLIGHT])
        tintin_printf(ses, "#THAT HIGHLIGHT IS NOT DEFINED.");
}


void do_all_high(char *line, struct session *ses)
{
    char text[BUFFER_SIZE];
    int attr[BUFFER_SIZE];
    int c, d;
    char *pos, *txt;
    int *atr;
    int l, r;
    struct listnode *ln;

    c=-1;
    txt=text;
    atr=attr;
    for (pos=line;*pos;pos++)
    {
        if (getcolor((const char**)&pos, &d, 1))
        {
            c=d;
            goto color;
        }
        *txt++=*pos;
        *atr++=c;
        continue;
color:
        ; /* Why the lack of a semicolon here causes a warning is beyond me. */
    };
    *txt=0;
    *atr=c;
    ln=ses->highs;
    while ((ln=ln->next))
    {
        txt=text;
        while (*txt&&find(txt, ln->left, &l, &r, ln->pr))
        {
            if (!get_high(ln->right))
                break;
            c=-1;
            r+=txt-text;
            l+=txt-text;
            /* changed: no longer highlight in the middle of a word */
            if (((l==0)||(!isalnum(text[l])||!isalnum(text[l-1])))&&
                    (!isalnum(text[r])||!isalnum(text[r+1])))
                for (int i=l;i<=r;i++)
                    attr[i]=highpattern[(++c)%nhighpattern];
            txt=text+r+1;
        }
    }
    c=-1;
    pos=line;
    txt=text;
    atr=attr;
    while (*txt)
    {
        if (c!=*atr)
            pos+=setcolor(pos, c=*atr);
        *pos++=*txt++;
        atr++;
        if (pos - line > BUFFER_SIZE - 20)
        {
            while (*txt)
                txt++, atr++;
            break;
        }
    }
    if (c!=*atr)
        pos+=setcolor(pos, c=*atr);
    *pos=0;
}
