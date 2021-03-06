#include "tintin.h"
#include "assert.h"
#include "protos/action.h"
#include "protos/glob.h"
#include "protos/globals.h"
#include "protos/ivars.h"
#include "protos/print.h"
#include "protos/misc.h"
#include "protos/parse.h"
#include "protos/regexp.h"
#include "protos/utils.h"
#include "protos/variables.h"

static int stacks[100][4];
static bool conv_to_ints(char *arg, struct session *ses);
static bool do_one_inside(int begin, int end);


/*********************/
/* the #math command */
/*********************/
void math_command(const char *line, struct session *ses)
{
    char left[BUFFER_SIZE], right[BUFFER_SIZE], temp[BUFFER_SIZE];
    int i;

    line = get_arg(line, left, 0, ses);
    line = get_arg(line, right, 1, ses);
    if (!*left||!*right)
    {
        tintin_eprintf(ses, "#Syntax: #math <variable> <expression>");
        return;
    }
    i = eval_expression(right, ses);
    sprintf(temp, "%d", i);
    set_variable(left, temp, ses);
}

/*******************/
/* the #if command */
/*******************/
struct session *if_command(const char *line, struct session *ses)
{
    char left[BUFFER_SIZE], right[BUFFER_SIZE];

    line = get_arg(line, left, 0, ses);
    line = get_arg_in_braces(line, right, 1);

    if (!*left || !*right)
    {
        tintin_eprintf(ses, "#ERROR: valid syntax is: if <condition> <command> [#elif <condition> <command>] [...] [#else <command>]");
        return ses;
    }

    if (eval_expression(left, ses))
        ses=parse_input(right, true, ses);
    else
    {
        line = get_arg_in_braces(line, left, 0);
        if (*left == tintin_char)
        {

            if (is_abrev(left + 1, "else"))
            {
                line = get_arg_in_braces(line, right, 1);
                ses=parse_input(right, true, ses);
            }
            if (is_abrev(left + 1, "elif"))
                ses=if_command(line, ses);
        }
    }
    return ses;
}


static bool do_inline(const char *line, int *res, struct session *ses)
{
    char command[BUFFER_SIZE], *ptr;

    ptr=command;
    while (*line&&(*line!=' '))
        *ptr++=*line++;
    *ptr=0;
    line=space_out(line);
    /*
       tintin_printf(ses, "#executing inline command [%c%s] with [%s]", tintin_char, command, line);
    */
    if (is_abrev(command, "finditem"))
        *res=finditem_inline(line, ses);
    else if (is_abrev(command, "isatom"))
        *res=isatom_inline(line, ses);
    else if (is_abrev(command, "listlength"))
        *res=listlength_inline(line, ses);
    else if (is_abrev(command, "strlen"))
        *res=strlen_inline(line, ses);
    else if (is_abrev(command, "random"))
        *res=random_inline(line, ses);
    else if (is_abrev(command, "grep"))
        *res=grep_inline(line, ses);
    else if (is_abrev(command, "strcmp"))
        *res=strcmp_inline(line, ses);
    else if (is_abrev(command, "match"))
        *res=match_inline(line, ses);
    else if (is_abrev(command, "ord"))
        *res=ord_inline(line, ses);
    else
    {
        tintin_eprintf(ses, "#Unknown inline command [%c%s]!", tintin_char, command);
        return false;
    }

    return true;
}


int eval_expression(char *arg, struct session *ses)
{
    if (!conv_to_ints(arg, ses))
        return 0;

    while (1)
    {
        int i = 0;
        bool flag = true;
        int begin = -1;
        int end = -1;
        int prev = -1;
        while (stacks[i][0] && flag)
        {
            if (stacks[i][1] == 0)
            {
                begin = i;
            }
            else if (stacks[i][1] == 1)
            {
                end = i;
                flag = false;
            }
            prev = i;
            i = stacks[i][0];
        }
        if ((flag && (begin != -1)) || (!flag && (begin == -1)))
        {
            tintin_eprintf(ses, "#Unmatched parentheses error in {%s}.", arg);
            return 0;
        }
        if (flag)
        {
            if (prev == -1)
                return stacks[0][2];
            begin = -1;
            end = i;
        }
        if (!do_one_inside(begin, end))
        {
            tintin_eprintf(ses, "#Invalid expression to evaluate in {%s}", arg);
            return 0;
        }
    }
}

static bool conv_to_ints(char *arg, struct session *ses)
{
    int i, flag;
    bool result, should_differ;
    bool regex=false; /* false=strncmp, true=regex match */
    char *ptr, *tptr;
    char temp[BUFFER_SIZE];
    char left[BUFFER_SIZE], right[BUFFER_SIZE];

    i = 0;
    ptr = arg;
    while (*ptr)
    {
        if (*ptr == ' ') ;
        else if (*ptr == tintin_char)
            /* inline commands */
        {
            ptr=(char*)get_inline(ptr+1, temp)-1;
            if (!do_inline(temp, &(stacks[i][2]), ses))
                return false;
            stacks[i][1]=15;
        }
        /* jku: comparing strings with = and != */
        else if (*ptr == '[')
        {
            ptr++;
            tptr=left;
            while ((*ptr) && (*ptr != ']') && (*ptr != '=') && (*ptr != '!'))
            {
                *tptr = *ptr;
                ptr++;
                tptr++;
            }
            *tptr='\0';
            if (!*ptr)
                return false; /* error */
            if (*ptr == ']')
                tintin_eprintf(ses, "#Compare %s to what ? (only one var between [ ])", left);
            /* fprintf(stderr, "Left argument = '%s'\n", left); */
            switch (*ptr)
            {
            case '!' :
                ptr++;
                should_differ=true;
                switch (*ptr)
                {
                case '=' : regex=false; ptr++; break;
                case '~' : regex=true; ptr++; break;
                default : return false;
                }
                break;
            case '=' :
                ptr++;
                should_differ=false;
                switch (*ptr)
                {
                case '=' : regex=false; ptr++; break;
                case '~' : regex=true; ptr++; break;
                default : break;
                }
                break;
            default : return false;
            }

            /* fprintf(stderr, "%c - %s match\n", (m) ? '=' : '!', (regex) ? "regex" : "string"); */

            tptr=right;
            while ((*ptr) && (*ptr != ']'))
            {
                *tptr = *ptr;
                ptr++;
                tptr++;
            }
            *tptr='\0';
            /* fprintf(stderr, "Right argument = '%s'\n", right); */
            if (!*ptr)
                return false;
            if (regex)
                result = !match(right, left);
            else
                result = strcmp(left, right);
            if (result == should_differ)
            { /* success */
                stacks[i][1] = 15;
                stacks[i][2] = 1;
                /* fprintf(stderr, "Expr TRUE\n"); */
            }
            else
            {
                stacks[i][1] = 15;
                stacks[i][2] = 0;
                /* fprintf(stderr, "Expr FALSE\n"); */
            }
        }
        /* jku: end of comparing strings */
        /* jku: undefined variables are now assigned value 0 (false) */
        else if (*ptr == '$')
        {
            if (ses->mesvar[MSG_VARIABLE])
                tintin_eprintf(ses, "#Undefined variable in {%s}.", arg);
            stacks[i][1] = 15;
            stacks[i][2] = 0;
            if (*(++ptr)==BRACE_OPEN)
            {
                ptr=(char*)get_arg_in_braces(ptr, temp, 0);
            }
            else
            {
                while (isalpha(*ptr) || *ptr=='_' || isadigit(*ptr))
                    ptr++;
            }
            ptr--;
        }
        /* jku: end of changes */
        else if (*ptr == '(')
            stacks[i][1] = 0;
        else if (*ptr == ')')
            stacks[i][1] = 1;
        else if (*ptr == '!')
            if (*(ptr + 1) == '=')
                stacks[i][1] = 12,
                ptr++;
            else
                stacks[i][1] = 2;
        else if (*ptr == '*')
        {
            stacks[i][1] = 3;
            stacks[i][3] = 0;
        }
        else if (*ptr == '/')
        {
            stacks[i][1] = 3;
            stacks[i][3] = 1;
        }
        else if (*ptr == '+')
        {
            stacks[i][1] = 5;
            stacks[i][3] = 2;
        }
        else if (*ptr == '-')
        {
            flag = -1;
            if (i > 0)
                flag = stacks[i - 1][1];
            if (flag == 15)
            {
                stacks[i][1] = 5;
                stacks[i][3] = 3;
            }
            else
            {
                tptr = ptr;
                ptr++;
                while (isadigit(*ptr))
                    ptr++;
                sscanf(tptr, "%d", &stacks[i][2]);
                stacks[i][1] = 15;
                ptr--;
            }
        }
        else if (*ptr == '>')
        {
            if (*(ptr + 1) == '=')
            {
                stacks[i][1] = 8;
                stacks[i][3] = 4;
                ptr++;
            }
            else
            {
                stacks[i][1] = 8;
                stacks[i][3] = 5;
            }
        }
        else if (*ptr == '<')
        {
            if (*(ptr + 1) == '=')
            {
                ptr++;
                stacks[i][1] = 8;
                stacks[i][3] = 6;
            }
            else
            {
                stacks[i][1] = 8;
                stacks[i][3] = 7;
            }
        }
        else if (*ptr == '=')
        {
            stacks[i][1] = 11;
            if (*(ptr + 1) == '=')
                ptr++;
        }
        else if (*ptr == '&')
        {
            stacks[i][1] = 13;
            if (*(ptr + 1) == '&')
                ptr++;
        }
        else if (*ptr == '|')
        {
            stacks[i][1] = 14;
            if (*(ptr + 1) == '|')
                ptr++;
        }
        else if (isadigit(*ptr))
        {
            stacks[i][1] = 15;
            tptr = ptr;
            while (isadigit(*ptr))
                ptr++;
            sscanf(tptr, "%d", &stacks[i][2]);
            ptr--;
        }
        else if (*ptr == 'T')
        {
            stacks[i][1] = 15;
            stacks[i][2] = 1;
        }
        else if (*ptr == 'F')
        {
            stacks[i][1] = 15;
            stacks[i][2] = 0;
        }
        else
        {
            tintin_eprintf(ses, "#Error. Invalid expression in #if or #math in {%s}.", arg);
            return false;
        }
        if (*ptr != ' ')
        {
            stacks[i][0] = i + 1;
            i++;
        }
        ptr++;
    }
    if (i > 0)
        stacks[i][0] = 0;
    return true;
}

static bool do_one_inside(int begin, int end)
{
    while (1)
    {
        int ptr = (begin > -1) ? stacks[begin][0] : 0;
        int highest = 16;
        int loc = -1;
        int ploc = -1;
        int prev = -1;
        while (ptr < end)
        {
            if (stacks[ptr][1] < highest)
            {
                highest = stacks[ptr][1];
                loc = ptr;
                ploc = prev;
            }
            prev = ptr;
            ptr = stacks[ptr][0];
        }

        if (highest == 15)
        {
            if (begin > -1)
            {
                stacks[begin][1] = 15;
                stacks[begin][2] = stacks[loc][2];
                stacks[begin][0] = stacks[end][0];
                return true;
            }
            else
            {
                stacks[0][0] = stacks[end][0];
                stacks[0][1] = 15;
                stacks[0][2] = stacks[loc][2];
                return true;
            }
        }
        else if (highest == 2)
        {
            int next = stacks[loc][0];
            if (stacks[next][1] != 15 || stacks[next][0] == 0)
                return false;
            stacks[loc][0] = stacks[next][0];
            stacks[loc][1] = 15;
            stacks[loc][2] = !stacks[next][2];
        }
        else
        {
            int next;
            assert(loc >= 0);
            next = stacks[loc][0];
            if (ploc == -1 || stacks[next][0] == 0 || stacks[next][1] != 15)
                return false;
            if (stacks[ploc][1] != 15)
                return false;
            switch (highest)
            {
            case 3:            /* highest priority is *,/ */
                stacks[ploc][0] = stacks[next][0];
                if (stacks[loc][3]==0)
                    stacks[ploc][2] *= stacks[next][2];
                else if (stacks[next][2])
                    stacks[ploc][2] /= stacks[next][2];
                else
                {
                    stacks[ploc][2]=0;
                    tintin_eprintf(0, "#Error: Division by zero.");
                }
                break;
            case 5:            /* highest priority is +,- */
                stacks[ploc][0] = stacks[next][0];
                if (stacks[loc][3]==2)
                    stacks[ploc][2] += stacks[next][2];
                else
                    stacks[ploc][2] -= stacks[next][2];
                break;
            case 8:            /* highest priority is >,>=,<,<= */
                stacks[ploc][0] = stacks[next][0];
                switch (stacks[loc][3])
                {
                case 5:
                    stacks[ploc][2] = (stacks[ploc][2] > stacks[next][2]);
                    break;
                case 4:
                    stacks[ploc][2] = (stacks[ploc][2] >= stacks[next][2]);
                    break;
                case 7:
                    stacks[ploc][2] = (stacks[ploc][2] < stacks[next][2]);
                    break;
                case 6:
                    stacks[ploc][2] = (stacks[ploc][2] <= stacks[next][2]);
                }
                break;
            case 11:            /* highest priority is == */
                stacks[ploc][0] = stacks[next][0];
                stacks[ploc][2] = (stacks[ploc][2] == stacks[next][2]);
                break;
            case 12:            /* highest priority is != */
                stacks[ploc][0] = stacks[next][0];
                stacks[ploc][2] = (stacks[ploc][2] != stacks[next][2]);
                break;
            case 13:            /* highest priority is && */
                stacks[ploc][0] = stacks[next][0];
                stacks[ploc][2] = (stacks[ploc][2] && stacks[next][2]);
                break;
            case 14:            /* highest priority is || */
                stacks[ploc][0] = stacks[next][0];
                stacks[ploc][2] = (stacks[ploc][2] || stacks[next][2]);
                break;
            default:
                tintin_eprintf(0, "#Programming error *slap Bill*");
                return false;
            }
        }
    }
}
