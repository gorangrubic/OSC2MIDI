//Spencer Jackson
//pair.c

//This should be sufficient to take a config file (.omm) line and have everything
//necessary to generate a midi message from the OSC and visa versa.

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<ctype.h>
#include"pair.h"

#include "ht_stuff.h"

typedef struct _PAIR
{

    //osc data
    char**   path;
    int* perc;//point in path string with printf format %
    int argc;
    int argc_in_path;
    char* types;

    //hash key and register values (pairs with the same hash key share the same register vector)
    int key;
    float* regs;

    //conversion factors, separate factors are kept for osc->midi vs midi->osc, they are equivalent but its necessary for one to many mappings
    int8_t *osc_map;           //which byte in the midi message by index of var in OSC message (including in path args)
    int8_t midi_map[4];        //which osc arg each midi value maps to
    float midi_scale[4];       //scale factor for each argument going into the midi message
    float midi_offset[4];      //linear offset for each arg going to midi message
    float *osc_scale;          //scale factor for each var in the osc message
    float *osc_offset;         //linear offset for each var in the osc message

    //midi constants 0- channel 1- data1 2- data2
    uint8_t opcode;
    uint8_t midi_rangemax[4]; //range bound for midi args (or same as val)
    uint8_t midi_val[4];      //constant values in midi args (or min of range)
    uint8_t n;                 //number of midi args for this opcode

    //osc constants
    float *osc_rangemax;   //range bounds for osc args (or same as val)
    float *osc_val;      //constant values for osc args (or min of range)

    //flags
    uint8_t use_glob_chan;  //flag decides if using global channel (1) or if its specified by message (0)
    uint8_t set_channel;    //flag if message is actually control message to change global channel
    uint8_t use_glob_vel;   //flag decides if using global velocity (1) or if its specified by message (0)
    uint8_t set_velocity;   //flag if message is actually control message to change global velocity
    uint8_t set_shift;      //flag if message is actually control message to change filter shift value
    uint8_t raw_midi;       //flag if message sends osc datatype of midi message
    uint8_t midi_const[4];  //flags for midi message (channel, data1, data2) is a constant (1) or range (2)
    uint8_t *osc_const;    //flags for osc args that are constant (1) or range (2)

} PAIR;


void print_pair(PAIRHANDLE ph)
{
    int i;
    PAIR* p = (PAIR*)ph;

    //path
    for(i=0; i<p->argc_in_path; i++)
    {
        p->path[i][p->perc[i]] = '{';
        printf("%s}",p->path[i]);
        p->path[i][p->perc[i]] = '%';
    }
    printf("%s",p->path[i]);

    //types
    printf(" %s, ",p->types);

    //arg names
    for(i=0; i<p->argc_in_path+p->argc; i++)
    {
        if(p->osc_const[i] == 2)
            printf("%.2f-%.2f",p->osc_val[i],p->osc_rangemax[i]);
        else if(p->osc_const[i] == 1)
            printf("%.2f",p->osc_val[i]);
        else
        {
            if(p->osc_map[i] != -1)
            {
                if(p->osc_scale[i] != 1)
                    printf("%.2f*",p->osc_scale[i]);
                printf("%c",'a'+p->midi_map[p->osc_map[i]]);//this is screwey but allows for duplicate osc args
                if(p->osc_offset[i])
                    printf(" + %.2f",p->osc_offset[i]);
            }
            else
                printf("x%i", i+1);
        }
        printf(", ");
    }

    //command
    printf("\b\b : %s",opcode2cmd(p->opcode, p->opcode==0x80 && p->n<4));
    printf("( ");

    //global channel
    if(p->use_glob_chan)
        printf("channel");
    //midi arg 0
    else if(p->midi_const[0] == 2)
        printf("%i-%i",p->midi_val[0], p->midi_rangemax[0]);
    else if(p->midi_const[0] == 1)
        printf("%i",p->midi_val[0]);
    else
    {
        if(p->midi_map[0] != -1)
        {
            if(p->midi_scale[0] != 1)
                printf("%.2f*",p->midi_scale[0]);
            printf("%c",'a'+p->midi_map[0]);
            if(p->midi_offset[0])
                printf(" + %.2f",p->midi_offset[0]);
        }
        else if(p->n>0)
            printf("y1");
    }

    //midi arg1
    if(p->opcode == 0xE0 && p->midi_const[1])
    {
        //pitchbend, use 14 bit values
        int min = p->midi_val[1]+128*p->midi_val[2];
        int max = p->midi_rangemax[1]+128*p->midi_rangemax[2];
        if(p->midi_const[1] == 2)
            printf(", %i-%i",min, max);
        else if(p->midi_const[1] == 1)
            printf(", %i",min);
    }
    else if(p->midi_const[1] == 2)
        printf(", %i-%i",p->midi_val[1], p->midi_rangemax[1]);
    else if(p->midi_const[1] == 1)
        printf(", %i",p->midi_val[1]);
    else
    {
        if(p->midi_map[1] != -1)
        {
            printf(", ");
            if(p->midi_scale[1] != 1)
                printf("%.2f*",p->midi_scale[1]);
            printf("%c",'a'+p->midi_map[1]);
            if(p->midi_offset[1])
                printf(" + %.2f",p->midi_offset[1]);
        }
        else if(p->n>1)
            printf(", y2");
    }

    //global velocity
    if(p->use_glob_vel)
        printf(", velocity");
    //midi arg2
    else if(p->midi_const[2] == 2)
        printf(", %i-%i",p->midi_val[2], p->midi_rangemax[2]);
    else if(p->midi_const[2] == 1)
        printf(", %i",p->midi_val[2]);
    else
    {
        if(p->midi_map[2] != -1)
        {
            printf(", ");
            if(p->midi_scale[2] != 1)
                printf("%.2f*",p->midi_scale[2]);
            printf("%c",'a'+p->midi_map[2]);
            if(p->midi_offset[2])
                printf(" + %.2f",p->midi_offset[2]);
        }
        else if(p->n>2)
            printf(", y3");
    }

    //midi arg3 (only for note on/off)
    if(p->midi_map[3] != -1)
    {
        printf(", ");
        if(p->midi_scale[3] != 1)
            printf("%.2f*",p->midi_scale[3]);
        printf("%c",'a'+p->midi_map[3]);
        if(p->midi_offset[3])
            printf(" + %.2f",p->midi_offset[3]);
    }
    else if(p->n>3)
        printf(", y4");//it shouldn't ever actually get here

    printf(" )\n");
}

int check_pair_set_for_filter(PAIRHANDLE* pa, int npairs)
{
    PAIR* p;
    int i;

    for(i=0; i<npairs; i++)
    {
        p = (PAIR*)pa[i];
        if(p->set_shift)
            return i+1;
    }
    return 0;
}

void rm_whitespace(char* str)
{
    int i,j;
    int n = strlen(str);
    for(i=j=0; j<=n; i++) //remove whitespace (and move null terminator)
    {
        while(str[j] == ' ' || str[j] == '\t')
        {
            j++;
        }
        str[i] = str[j++];
    }
}

//do a quick syntax check of a config line -ag

/* This is in fact a full parser for the omm config line syntax. It doesn't do
   any actual semantic processing right now, but simply does a thorough check
   of the context-free syntax, so that we can be sure that it's what we expect
   before rescanning the line in the semantic routines below.

   We parse the following syntax (in EBNF):

   rule ::= path argtypes ',' [ arglist ] ':' command '(' arglist ')'

   path ::= OSC path string, must be non-empty

   argtypes ::= OSC type string, may be empty

   arglist ::= [ arg ] { ',' [ arg ] }

   arg ::= [ pre ] var [ post ] | number | number '-' number

   pre ::= '-' | [ number add-op ] [ number '*' ]

   post ::= [ mul-op number ] [ add-op number ]

   add-op ::= '+' | '-'

   mul-op ::= '*' | '/'

   var ::= may contain anything but operators, comma and ':' or ')' delimiter,
           must not be empty

   NOTES:

   To accommodate the widest possible range of OSC applications, the syntax is
   deliberately lenient with the OSC path syntax and will accept any string
   not containing whitespace. Therefore path and argtypes *must* be delimited
   by whitespace (even if argtypes is empty).

   In contrast, the parser is fairly picky about the argtypes string and only
   allows valid OSC type symbols there.

   In order to provide good error-checking, the parser is also picky about the
   end of the line (anything which comes after the mapping rule). We only
   permit whitespace there, any number of semicolons and a line-end comment
   (# ...), anything else is flagged as an error. */

#define error_exit(msg,s) { msg = s; goto errout; }
#define error_check(msg) if (msg) goto errout;

char* check_arg(char* s, char delim, char **msg)
{
    char *t;
    int pre = 0, var = 0;
    // optional conditioning prefix: a[+-]b* | a[+-] | b* | - (unary minus)
    if (*s == '-' && !isdigit(s[1]) && s[1] != '.')
    {
        // unary minus
        pre = 1;
        s++;
        while (isspace(*s)) s++;
    }
    else
    {
        (void)strtod(s, &t);
        if (t > s)
        {
            pre = 1;
            // number
            s = t;
            while (isspace(*s)) s++;
            char op = *s;
            if (!op || !strchr("+-*", op))
                // just a number
                return s;
            s++;
            while (isspace(*s)) s++;
            if (op != '*')
            {
                (void)strtod(s, &t);
                if (t > s)
                {
                    // number
                    s = t;
                    while (isspace(*s)) s++;
                    char op2 = *s;
                    if (op == '-' && (op2 == ',' || op2 == delim))
                    {
                        // a range
                        return s;
                    }
                    if (op2 != '*')
                        error_exit(*msg, "expected '*'");
                    s++;
                    while (isspace(*s)) s++;
                }
            }
        }
    }
    // variable
    // may contain anything but whitespace, operator, comma and delim symbol
    while (*s && !isspace(*s) && !strchr("+-*/,", *s) && *s!=delim)
    {
        var = 1;
        s++;
    }
    if (pre && !var) error_exit(*msg, "expected variable");
    while (isspace(*s)) s++;
    // optional conditioning postfix: [*/]a[+-]b | [+-]b | [*/]a
    char op = *s;
    if (op && strchr("+-*/", op))
    {
        if (!var) error_exit(*msg, "expected variable");
        s++;
        while (isspace(*s)) s++;
        (void)strtod(s, &t);
        if (t == s) error_exit(*msg, "expected number");
        s = t;
        while (isspace(*s)) s++;
        if (op != '+' && op != '-')
        {
            char op2 = *s;
            if (op2 && strchr("+-", op2))
            {
                s++;
                while (isspace(*s)) s++;
                (void)strtod(s, &t);
                if (t == s) error_exit(*msg, "expected number");
                s = t;
                while (isspace(*s)) s++;
            }
        }
    }
errout:
    return s;
}

int check_config(char* config)
{
    char *s = config, *msg = 0;

    while (isspace(*s)) s++;
    // OSC path
    if (!*s) error_exit(msg, "expected osc path");
    while (*s && !isspace(*s)) s++;
    while (isspace(*s)) s++;
    // OSC type string is optional
    while (*s && !isspace(*s) && *s!=',' && *s!=':')
    {
        if (!strchr("ihsSbfdtcmTFNI", *s)) error_exit(msg, "unknown OSC type");
        s++;
    }
    if (*s != ',') error_exit(msg, "expected ','");
    s++;
    while (isspace(*s)) s++;
    // OSC arguments are optional
    while (*s && *s!=':')
    {
        s = check_arg(s, ':', &msg);
        error_check(msg);
        if (!*s || *s == ':') break;
        if (*s != ',') error_exit(msg, "expected ',' or ':'");
        s++;
        while (isspace(*s)) s++;
    }
    if (*s != ':') error_exit(msg, "expected ':'");
    s++;
    while (isspace(*s)) s++;
    // MIDI command name
    if (!*s || *s=='(') error_exit(msg, "expected midi command");
    while (*s && !isspace(*s) && *s!='(') s++;
    while (isspace(*s)) s++;
    // MIDI command arguments
    if (*s != '(') error_exit(msg, "expected '('");
    s++;
    while (isspace(*s)) s++;
    if (!*s || *s==')') error_exit(msg, "expected midi arguments");
    while (*s && *s!=')')
    {
        s = check_arg(s, ')', &msg);
        error_check(msg);
        if (!*s || *s == ')') break;
        if (*s != ',') error_exit(msg, "expected ',' or ')'");
        s++;
        while (isspace(*s)) s++;
    }
    if (*s != ')') error_exit(msg, "expected ')'");
    s++;
    // Check the line end (everything that comes after the rule). We allow a
    // trailing semicolon, end-of-line comment and whitespace there, flag
    // everything else as an error.
    while (isspace(*s) || *s==';') s++;
    if (*s && *s != '#') error_exit(msg, "expected end of line or comment");

errout:
    if (msg)
    {
        char mark[400];
        int i, n = s-config;
        if (n>0 && config[n-1]=='\n') n--;
        strncpy(mark, config, n);
        for (i = 0; i < n; i++)
            if (!isspace(mark[i]))
                mark[i] = ' ';
        strcpy(mark+n, "^^^\n");
        printf("\nERROR in config line:\n%s%s -syntax error: %s\n\n",config,mark,msg);
        return -1;
    }
    else
        return 0;
}

int get_pair_path(char* config, char* path, PAIR* p)
{
    char* tmp,*prev;
    int n,i,j = 0;
    char var[100];
    if(!sscanf(config,"%s %*[^,],%*[^:]:%*[^(](%*[^)])",path))
    {
        printf("\nERROR in config line:\n%s -could not get OSC path!\n\n",config);
        return -1;
    }

    //decide if path has some arguments in it
    //figure out how many chunks it will be broken up into
    prev = path;
    tmp = path;
    i = 1;
    while( (tmp = strchr(tmp,'{')) )
    {
        i++;
        tmp++;
    }
    p->path = (char**)malloc(sizeof(char*)*i);
    p->perc = (int*)malloc(sizeof(int)*(i-1));
    //now break it up into chunks
    while( (tmp = strchr(prev,'{')) )
    {
        //get size of this part of path and allocate a string
        n = tmp - prev;
        i = 0;
        tmp = prev;
        while( (tmp = strchr(tmp,'%')) ) i++;
        p->path[p->argc_in_path] = (char*)malloc(sizeof(char)*n+i+3);
        //double check the variable is good
        i = sscanf(prev,"%*[^{]{%[^}]}",var);
        if(i < 1 || !strchr(var,'i'))
        {
            printf("\nERROR in config line:\n%s -could not get variable in OSC path, use \'{i}\'!\n\n",config);
            return -1;
        }
        //copy over path segment, delimit any % characters
        j=0;
        for(i=0; i<n; i++)
        {
            if(prev[i] == '%')
                p->path[p->argc_in_path][j++] = '%';
            p->path[p->argc_in_path][j++] = prev[i];
        }
        p->path[p->argc_in_path][j] = 0;//null terminate to be safe
        p->perc[p->argc_in_path] = j;//mark where the format percent sign is
        strcat(p->path[p->argc_in_path++],"%i");
        prev = strchr(prev,'}');
        prev++;
    }
    //allocate space for end of path and copy
    p->path[p->argc_in_path] = (char*)malloc(sizeof(char)*(strlen(prev)+1));
    strcpy(p->path[p->argc_in_path],prev);
    return 0;
}

int get_pair_argtypes(char* config, char* path, PAIR* p, table tab, float** regs, int* nkeys)
{
    char argtypes[100];
    int i,j = 0;
    uint8_t len;
    if(!sscanf(config,"%*s %[^,],%*[^:]:%*[^(](%*[^)])",argtypes))
    {
        //it could be an error or it just doesn't have any args
        //printf("\nERROR in config line:\n%s -could not get OSC data types!\n\n",config);
        //return -1;
        strcpy(argtypes,"");
    }

    //allocate space for the number of arguments
    len = strlen(argtypes);
    p->types = (char*)malloc( sizeof(char) * (len+1) );
    p->osc_map = (int8_t*)malloc( sizeof(int8_t) * (p->argc_in_path+len+1) );
    p->osc_scale = (float*)malloc( sizeof(float) * (p->argc_in_path+len+1) );
    p->osc_offset = (float*)malloc( sizeof(float) * (p->argc_in_path+len+1) );
    p->osc_const = (uint8_t*)malloc( sizeof(uint8_t) * (p->argc_in_path+len+1) );
    p->osc_val = (float*)malloc( sizeof(float) * (p->argc_in_path+len+1) );
    p->osc_rangemax = (float*)malloc( sizeof(float) * (p->argc_in_path+len+1) );

    //initialize hash key and register storage -ag
    p->key = strkey(tab, path, argtypes, nkeys);
    p->regs = regs[p->key];
    //allocate space for the register storage if not yet initialized
    if(!p->regs)
    {
        p->regs = regs[p->key] = (float*)calloc( p->argc_in_path+len+1, sizeof(float) );
    }

    //now get the argument types
    for(i=0; i<len; i++)
    {
        switch(argtypes[i])
        {
        case 'i':
        case 'h'://long
        case 's'://string
        case 'b'://blob
        case 'f':
        case 'd':
        case 'S'://symbol
        case 't'://timetag
        case 'c'://char
        case 'm'://midi
        case 'T'://true
        case 'F'://false
        case 'N'://nil
        case 'I'://infinity
            p->types[j++] = argtypes[i];
            p->argc++;
        case ' ':
            break;
        default:
            printf("\nERROR in config line:\n%s -argument type '%c' not supported!\n\n",config,argtypes[i]);
            return -1;
            break;
        }
    }
    p->types[j] = 0;//null terminate. It's good practice
    for(i=0; i<len+p->argc_in_path; i++)
        p->osc_map[i] = -1;//initialize mapping for all args
    return 0;
}

int get_pair_midicommand(char* config, PAIR* p)
{
    char midicommand[100];
    int n;
    if(!sscanf(config,"%*s%*[^,],%*[^:]:%[^(](%*[^)])",midicommand))
    {
        printf("\nERROR in config line:\n%s -could not get MIDI command!\n\n",config);
        return -1;
    }

    //next the midi command
    /*
      noteon( channel, noteNumber, velocity );
      noteoff( channel, noteNumber, velocity );
      note( channel, noteNumber, velocity, state );  # state dictates note on (state != 0) or note off (state == 0)
      polyaftertouch( channel, noteNumber, pressure );
      controlchange( channel, controlNumber, value );
      programchange( channel, programNumber );
      aftertouch( channel, pressure );
      pitchbend( channel, value );
      rawmidi( byte0, byte1, byte2 );  # this sends whater midi message you compose with bytes 0-2
      midimessage( message );  # this sends a message using the OSC type m which is a pointer to a midi message

         non-Midi functions that operate other system functions are:
      setchannel( channelNumber );  # set the global channel
      setshift( noteOffset ); # set the midi note filter shift amout
    */
    rm_whitespace(midicommand);
    if(!strcmp(midicommand,"noteon"))
    {
        p->opcode = 0x90;
        n = 3;
    }
    else if(!strcmp(midicommand,"noteoff"))
    {
        p->opcode = 0x80;
        n = 3;
    }
    else if(!strcmp(midicommand,"note"))
    {
        p->opcode = 0x80;
        n = 4;
    }
    else if(!strcmp(midicommand,"polyaftertouch"))
    {
        p->opcode = 0xA0;
        n = 3;
    }
    else if(!strcmp(midicommand,"controlchange"))
    {
        p->opcode = 0xB0;
        n = 3;
    }
    else if(!strcmp(midicommand,"programchange"))
    {
        p->opcode = 0xC0;
        n = 2;
    }
    else if(!strcmp(midicommand,"aftertouch"))
    {
        p->opcode = 0xD0;
        n = 2;
    }
    else if(!strcmp(midicommand,"pitchbend"))
    {
        p->opcode = 0xE0;
        n = 2;
    }
    else if(!strcmp(midicommand,"rawmidi"))
    {
        p->opcode = 0x00;
        n = 3;
        p->raw_midi = 1;
    }
    else if(!strcmp(midicommand,"midimessage"))
    {
        p->opcode = 0x01;
        p->midi_val[0] = 0xFF;
        n = 1;
        p->raw_midi = 1;
    }
    //non-midi commands
    else if(!strcmp(midicommand,"setchannel"))
    {
        p->opcode = 0x02;
        p->midi_val[0] = 0xFE;
        n = 1;
        p->set_channel = 1;
    }
    else if(!strcmp(midicommand,"setvelocity"))
    {
        p->opcode = 0x03;
        p->midi_val[0] = 0xFD;
        n = 1;
        p->set_velocity = 1;
    }
    else if(!strcmp(midicommand,"setshift"))
    {
        p->opcode = 0x04;
        p->midi_val[0] = 0xFC;
        n = 1;
        p->set_shift = 1;
    }
    else
    {
        printf("\nERROR in config line:\n%s -midi command %s unknown!\n\n",config,midicommand);
        return -1;
    }
    p->n = n;
    return n;
}

//this gets the alpha-numeric variable name, ignoring conditioning
//returns 0 if no var found
int get_pair_arg_varname(char* arg, char* varname)
{
    //just get the name and return if successful
    int j=0;
    if( !(j = sscanf(arg,"%*[.1234567890*/+- ]%[^*/+- ,:]%*[.1234567890*/+- ]",varname)) )
    {
        j = sscanf(arg,"%[^*/+- ,]%*[.1234567890*/+- ]",varname);
        if(varname[0] >= '0' && varname[0] <= '9')
            return 0; //don't allow 1st character be a number
    }
    return j;
}

//this checks if it is a constant or range and returns val and rangemax
//accordingly. val will be the min for the range
//returns 2 if range, 1 if const, 0 if niether
int get_pair_arg_constant(char* arg, float* val, float* rangemax)
{
    uint8_t n;
    char tmp[40];
    *val = 0;
    *rangemax = 0;
    if(0 < get_pair_arg_varname(arg,tmp))
    {
        //Not a constant, has a variable
        return 0;
    }
    //must be a constant or range
    n = sscanf(arg,"%f%*[- ]%f%*[ ,]",val,rangemax);
    if(n==2)
    {
        //range
        return 2;
    }
    else if(n==1)
    {
        //constant
        *rangemax = *val;
        return 1;
    }
    else
    {
        //neither
        return 0;
    }
}

//these are used to find the actual mapping
int get_pair_osc_arg_index(char* varname, char* oscargs, uint8_t argc, uint8_t skip)
{
    //find where it is in the OSC message
    uint8_t i;
    char name[50] = "";
    int k = strlen(varname);
    char* tmp = oscargs;

    for(i=0; i<argc; i++)
    {
        if(tmp[0] != ',')
        {
            //argument is not blank
            if(get_pair_arg_varname(tmp, name))
            {
                if(!strncmp(varname,name,k) && k == strlen(name))
                {
                    //it's a match
                    if(!skip--)
                        return i;
                }
            }
            else
            {
                //could not understand conditioning, check if constant
                float f,f2;
                if(!get_pair_arg_constant(tmp,&f,&f2))
                    return -1;
            }
        }
        //next arg name
        tmp = strchr(tmp,',');
        tmp++;//go to char after ','
        if(tmp[0] == 0)
        {
            //underspecified (no more variable names)
            return -1;
        }
    }
    if(i == argc)
    {
        //var is not used
        return -1;
    }
    return i;
}

//check whether the given string contains nothing but whitespace
static int is_ws(const char *s)
{
    while(*s && isspace(*s)) s++;
    return *s==0;
}

//match the given string against a given operator symbol
//checks that the string contains nothing but the operator symbol,
//possibly surrounded by whitespace
static int match_op(const char *s, char op)
{
    while(*s && isspace(*s)) s++;
    if (*s != op) return 0;
    while(*++s && isspace(*s));
    return *s==0;
}

//this function assumes scale and offset are initialized to 1 and 0 (or more appropriate numbers)
//it will simpy add in (or multiply in) the values found here
//it will populate the varname string so it must have memory allocated
int get_pair_arg_conditioning(char* arg, char* varname, float* _scale, float* _offset)
{
    //make sure that these are initialized properly, even if never matched
    char pre[50] = {0}, post[50] = {0};
    uint8_t j;
    float scale = 1, offset = 0;
    //This is a bit of a hack, but we use a bunch of %n's here to make sure
    //that we don't leave any trailing random garbage in any of the sscanf
    //calls below. Any such leftovers indicate syntax errors, unless they're
    //nothing but whitespace, so we need to verify that. -ag
    int end = strlen(arg);
    if( !(j = sscanf(arg,"%[.1234567890*/+- ]%n%[^*/+- ]%n%[.1234567890*/+- ]%n",pre,&end,varname,&end,post,&end)) )
    {
        j = sscanf(arg,"%[^*/+- ]%n%[.1234567890*/+- ]%n",varname,&end,post,&end);
    }
    if(!j || !is_ws(arg+end)) //don't allow trailing garbage
    {
        printf("\nERROR -could not parse arg '%s'!\n", arg);
        return -1;
    }
    //get conditioning, should be pre=b+a* and/or post=*a+b
    if(*pre)
    {
        char s1[20],s2[20];
        float a,b;
        switch(sscanf(pre,"%f%[-+* ]%n%f%n%[+-* ]%n",&b,s1,&end,&a,&end,s2,&end))
        {
        case 4:
            if(match_op(s2,'*'))//only multiply makes sense here
            {
                scale *= a;
            }
            else
            {
                printf("\nERROR -failed to parse '%s'! nonsensical operator?\n", pre);
                return -1;
            }
        case 3:
        //if its not whitespace, its nonsensical, will be caught below
        case 2:
            if (!is_ws(pre+end)) //don't allow trailing garbage
            {
                printf("\nERROR -failed to parse '%s'! nonsensical operator?\n", pre);
                return -1;
            }
            if(match_op(s1,'*'))
            {
                scale *= b;
            }
            else if(match_op(s1,'+'))
            {
                offset += b;
            }
            else if(match_op(s1,'-'))
            {
                offset += b;
                scale *= -1;
            }
            else
            {
                printf("\nERROR -failed to parse '%s'! nonsensical operator?\n", pre);
                return -1;
            }
            break;
        default:
            if(match_op(pre,'-'))
            {
                scale *= -1;
            }
            //if we come here, we failed to parse the pre conditioning; if
            //it's just whitespace then we ignore it, otherwise there's a
            //syntax error, so spit out an error message
            else if (!is_ws(pre))
            {
                printf("\nERROR -failed to parse '%s'! nonsensical operator?\n", pre);
                return -1;
            }
            break;
        }//switch
    }//if pre conditions
    if(*post)
    {
        char s1[20],s2[20];
        float a,b;
        switch(sscanf(post,"%[-+*/ ]%f%n%[+- ]%n%f%n",s1,&a,&end,s2,&end,&b,&end))
        {
        case 4:
            if(match_op(s2,'+'))//only add/subtract makes sense here
            {
                offset += b;
            }
            else if(match_op(s2,'-'))
            {
                offset -= b;
            }
            else
            {
                printf("\nERROR -failed to parse '%s'! nonsensical operator?\n", post);
                return -1;
            }
        case 3:
        //if its not whitespace, its nonsensical, will be caught below
        case 2:
            if (!is_ws(post+end)) //don't allow trailing garbage
            {
                printf("\nERROR -failed to parse '%s'! nonsensical operator?\n", post);
                return -1;
            }
            if(match_op(s1,'*'))
            {
                scale *= a;
            }
            else if(match_op(s1,'/'))
            {
                scale /= a;
            }
            else if(match_op(s1,'+'))
            {
                offset += a;
            }
            else if(match_op(s1,'-'))
            {
                offset -= a;
            }
            else
            {
                printf("\nERROR -failed to parse '%s'! nonsensical operator?\n", post);
                return -1;
            }
            break;
        default:
            //if we come here, we failed to parse the post conditioning; if
            //it's just whitespace then we ignore it, otherwise there's a
            //syntax error, so spit out an error message
            if (!is_ws(post))
            {
                printf("\nERROR -failed to parse '%s'! nonsensical operator?\n", post);
                return -1;
            }
            break;
        }//switch

    }//if post conditioning
    *_scale *= scale;
    *_offset += offset;
    return 0;
}


//for each midi args
//  check if constant, range or keyword (glob chan etc)
//  else go through osc args to find match
//  get conditioning
//for each osc arg
//  if not used, check if constant
//  if used get conditioning
int get_pair_mapping(char* config, PAIR* p, int n)
{
    char argnames[200],midiargs[200],
         arg0[70], arg1[70], arg2[70], arg3[70],
         var[50];
    char *tmp, *marg[4];
    float f,f2;
    int i,j;
    int8_t k;

    arg0[0]=0;
    arg1[0]=0;
    arg2[0]=0;
    arg3[0]=0;
    if(2 < sscanf(config,"%*s%*[^,],%[^:]:%*[^(](%[^)])",argnames,midiargs))
    {
        if(!sscanf(config,"%*s%*[^,],%*[^:]:%*[^(](%[^)])",midiargs))
        {
            printf("\nERROR in config line:\n%s -could not get MIDI command arguments!\n\n",config);
            return -1;
        }
        argnames[0] = 0;//all constants in midi command
    }

    //lets get those midi arguments
    rm_whitespace(argnames);
    strcat(argnames,",");//add trailing comma
    i = sscanf(midiargs,"%[^,],%[^,],%[^,],%[^,]",arg0,arg1,arg2,arg3);
    if(n != i)
    {
        printf("\nERROR in config line:\n%s -incorrect number of args in midi command!\n\n",config);
        return -1;
    }

    //and the most difficult part: the mapping
    marg[0] = arg0;
    marg[1] = arg1;
    marg[2] = arg2;
    marg[3] = arg3;
    for(i=0; i<n; i++) //go through each argument
    {
        //default values
        p->midi_map[i] = -1;//assume args not used
        p->midi_scale[i] = 1;
        p->midi_offset[i] = 0;
        p->midi_const[i] = 0;
        p->midi_val[i] = p->midi_rangemax[i] = 0;

        if((j = get_pair_arg_constant(marg[i],&f,&f2)))
        {
            //it's constant
            if(i == 3)
            {
                //for some reason they used the "note" command but then put a constant in whether it is on or off
                if(f)p->opcode+=0x10;
                p->n = 3;
            }
            else
            {
                p->midi_val[i] = ((uint8_t)f)&0x7f;
                p->midi_rangemax[i] = ((uint8_t)f2)&0x7f;
                p->midi_const[i] = j;
                if(p->opcode == 0xE0 && i == 1)
                {
                    //pitchbend, store the MSB
                    ++i;
                    p->midi_val[i] = ((uint8_t)(f/128.0))&0x7f;
                    p->midi_rangemax[i] = ((uint8_t)(f2/128.0))&0x7f;
                    //default values
                    p->midi_map[i] = -1;
                    p->midi_scale[i] = 1;
                    p->midi_offset[i] = 0;
                    p->midi_const[i] = 0;
                }
            }
        }
        else if(-1 == get_pair_arg_conditioning(marg[i], var, &p->midi_scale[i], &p->midi_offset[i]))//get conditioning for midi arg
        {
            printf("\nERROR in config line:\n%s -could not understand arg %i in midi command\n\n",config,i);
            return -1;
        }
        else if(!strcmp(var,"channel"))//check if its the global channel keyword
        {
            if (i != 0)
            {
                printf("\nERROR in config line:\n%s -special channel variable used in wrong position (arg %i) in midi command\n\n",config,i);
                return -1;
            }
            if (p->midi_scale[i] != 1.0 || p->midi_offset[i] != 0.0)
            {
                printf("\nERROR in config line:\n%s -scaling of special channel variable not supported\n\n",config);
                return -1;
            }
            p->use_glob_chan = 1;//should these global vars be able to be scaled?
        }
        else if(!strcmp(var,"velocity"))
        {
            if (i != 2)
            {
                printf("\nERROR in config line:\n%s -special velocity variable used in wrong position (arg %i) in midi command\n\n",config,i);
                return -1;
            }
            if (p->midi_scale[i] != 1.0 || p->midi_offset[i] != 0.0)
            {
                printf("\nERROR in config line:\n%s -scaling of special velocity variable not supported\n\n",config);
                return -1;
            }
            p->use_glob_vel = 1;
        }
        else if(p->midi_scale[i] == 0.0)
        {
            //zero scaling factor, treated as a constant
            printf("\nWARNING in config line:\n%s -arg %i in midi command has zero scaling factor, treated as a constant\n\n",config,i);
            if(i == 3)
            {
                if(f)p->opcode+=0x10;
                p->n = 3;
            }
            else
            {
                p->midi_val[i] = p->midi_rangemax[i] = p->midi_offset[i];
                p->midi_const[i] = 1;
            }
        }
        else
        {
            //get mapping
            k=0;
            j = get_pair_osc_arg_index(var, argnames, p->argc_in_path + p->argc,k++);
            if(j >=0 )
                p->midi_map[i] = j;
            while(j >=0)
            {
                p->osc_map[j] = i;
                //check for additional copies
                j = get_pair_osc_arg_index(var, argnames, p->argc_in_path + p->argc,k++);
            }
        }
    }//for each midi arg
    for(; i<4; i++) //to be safe set the rest to default values even though unused
    {
        //default values
        p->midi_map[i] = -1;//assume args not used
        p->midi_scale[i] = 1;
        p->midi_offset[i] = 0;
        p->midi_const[i] = 0;
        p->midi_val[i] = p->midi_rangemax[i] = 0;
    }

    //now go through OSC args
    tmp = strtok(argnames,",");
    for(i=0; i<p->argc_in_path + p->argc; i++)
    {
        if(!tmp)
        {
            //underspecified, assume everything else is unused
            for(; i<p->argc_in_path + p->argc; i++)
            {
                p->osc_val[i] = 0;
                p->osc_scale[i] = 1;
                p->osc_offset[i] = 0;
                p->osc_rangemax[i] = 0;
                p->osc_const[i] = 0;
            }
            break;
        }
        p->osc_val[i] = 0;
        p->osc_scale[i] = 1;
        p->osc_offset[i] = 0;
        p->osc_rangemax[i] = 0;
        p->osc_const[i] = 0;
        k = p->osc_map[i];
        if(k != -1)
        {
            //it's  used, check if it is  conditioned
            float scale=1, offset=0;
            if(-1 == get_pair_arg_conditioning(tmp, var, &scale, &offset))//get conditioning for osc arg
            {
                printf("\nERROR in config line:\n%s -could not get OSC arg conditioning for arg %s!\n\n",config,tmp);
                return -1;
            }
            if(scale == 0.0)
            {
                //zero scaling factor, treated as a constant
                printf("\nWARNING in config line:\n%s -OSC arg %s has zero scaling factor, treated as a constant\n\n",config,tmp);
                p->osc_val[i] = p->osc_rangemax[i] = offset;
                p->osc_const[i] = 1;
                //remove the variable binding
                p->osc_map[i] = -1;
                //check to see if there's still another binding for this
                //variable and update the midi mapping accordingly
                for(j = i+1; j < p->argc_in_path + p->argc; j++)
                    if(p->osc_map[j] == k)
                        break;
                if(j >= p->argc_in_path + p->argc) j = -1;
                for(k = 0; k < p->n; k++)
                    if(p->midi_map[k] == i)
                        p->midi_map[k] = j;
            }
            else
            {
                //scale and offset are inverted in osc
                p->osc_scale[i] = scale;
                p->osc_offset[i] = offset;
            }
        }
        else
        {
            //it's not used, check if it is constant or range
            p->osc_const[i] = get_pair_arg_constant(tmp,&p->osc_val[i],&p->osc_rangemax[i]);
        }
        //next arg name
        tmp = strtok(NULL,",");
    }
    return 0;
}

PAIRHANDLE abort_pair_alloc(int step, PAIR* p)
{
    switch(step)
    {
    case 3:
        free(p->types);
        free(p->osc_map);
        free(p->osc_scale);
        free(p->osc_offset);
        free(p->osc_const);
        free(p->osc_val);
        free(p->osc_rangemax);
    case 2:
        while(p->argc_in_path >=0)
        {
            free(p->path[p->argc_in_path--]);
        }
        free(p->perc);
        free(p->path);
    case 1:
        free(p);
    default:
        break;
    }
    return 0;
}

PAIRHANDLE alloc_pair(char* config, table tab, float** regs, int* nkeys)
{
    //path argtypes, arg1, arg2, ... argn : midicommand(arg1+4, arg3, 2*arg4);
    PAIR* p;
    int n;
    char path[200];

    //for cosmetic purposes, add a line end if necessary (no newline at eof)
    if (!strchr(config, '\n')) strcat(config, "\n");
    //do a quick syntax check
    if(-1 == check_config(config))
        return 0;

    p = (PAIR*)calloc(1, sizeof(PAIR));

    //set defaults
    p->argc_in_path = 0;
    p->argc = 0;
    p->raw_midi = 0;
    p->opcode = 0;
    p->midi_val[0] = 0;
    p->midi_val[1] = 0;
    p->midi_val[2] = 0;

    //config into separate parts
    if(-1 == get_pair_path(config,path,p))
        return abort_pair_alloc(2,p);


    if(-1 == get_pair_argtypes(config,path,p,tab,regs,nkeys))
        return abort_pair_alloc(3,p);


    n = get_pair_midicommand(config,p);
    if(-1 == n)
        return abort_pair_alloc(3,p);


    if(-1 == get_pair_mapping(config,p,n))
        return abort_pair_alloc(3,p);

    return p;//success
}

void free_pair(PAIRHANDLE ph)
{
    PAIR* p = (PAIR*)ph;
    free(p->types);
    free(p->osc_map);
    free(p->osc_scale);
    free(p->osc_offset);
    free(p->osc_const);
    free(p->osc_val);
    free(p->osc_rangemax);
    while(p->argc_in_path >=0)
    {
        free(p->path[p->argc_in_path--]);
    }
    free(p->perc);
    free(p->path);
    free(p);
}

//returns 1 if match is successful and msg has a message to be sent to the output
int try_match_osc(PAIRHANDLE ph, char* path, char* types, lo_arg** argv, int argc, uint8_t strict_match, uint8_t* glob_chan, uint8_t* glob_vel, int8_t *filter, uint8_t msg[])
{
    PAIR* p = (PAIR*)ph;
    //check the easy things first
    if(argc < p->argc)
    {
        return 0;
    }
    if(strncmp(types,p->types,strlen(p->types)))//this won't work if it switches between T and F
    {
        return 0;
    }


    //set defaults / static data
    msg[0] = p->opcode;
    if (p->set_channel || p->set_velocity || p->set_shift)
    {
        // Special calls to set channel, velocity etc.
        msg[1] = p->midi_val[0];
        msg[2] = 0;
    }
    else
    {
        // Regular call which generates a MIDI message.
        msg[0] += p->midi_val[0];
        msg[1] = p->midi_val[1];
        msg[2] = p->midi_val[2];
        if(p->use_glob_chan)
        {
            msg[0] += *glob_chan;
        }
        if(p->use_glob_vel)
        {
            msg[2] += *glob_vel;
        }
    }

    //now start trying to get the data
    int i,v,n;
    char *tmp;
    int place;
    float conditioned;
    //check path
    for(i=0; i<p->argc_in_path; i++)
    {
        //does it match?
        p->path[i][p->perc[i]] = 0;
        tmp = strstr(path,p->path[i]);
        n = strlen(p->path[i]);
        p->path[i][p->perc[i]] = '%';
        if( tmp !=path )
        {
            return 0;
        }
        //get the argument
        if(!sscanf(tmp,p->path[i],&v))
        {
            return 0;
        }
        //put it in the message;
        place = p->osc_map[i];
        if(place != -1)
        {
            //place only indicates one of the places that a variable occurs in
            //the midi mapping, so in order to catch all instances of the same
            //variable, we have to iterate over all midi arguments bound to the
            //given osc argument here -ag
            for(place=0; place<p->n; place++)
            {
                if(p->midi_map[place]==i)
                {
                    conditioned = p->midi_scale[place]*(v - p->osc_offset[i])/p->osc_scale[i] + p->midi_offset[place];
                    //same code as below for arguments to set global channel etc.
                    //assert place==0 here
                    if(p->set_channel)
                    {
                        if(conditioned<0) conditioned = 0;
                        if(conditioned>15) conditioned = 15;
                        msg[place+1] = ((uint8_t)conditioned);
                    }
                    else if(p->set_velocity)
                    {
                        if(conditioned<0) conditioned = 0;
                        if(conditioned>127) conditioned = 127;
                        msg[place+1] = ((uint8_t)conditioned);
                    }
                    else if(p->set_shift)
                    {
                        msg[place+1] = ((uint8_t)conditioned);
                    }
                    //put it in the midi message
                    else if(place == 3)//only used for note on or off
                    {
                        msg[0] += ((uint8_t)(conditioned>0))<<4;
                    }
                    else
                    {
                        //clamp MIDI values
                        // - 0..255 for status bytes (arg #0 of rawmidi)
                        // - 0..15 for channel (arg #0 of other commands)
                        // - 0..127 for regular data bytes
                        // - 0..16383 for pitch bend
                        if(conditioned<0) conditioned = 0;
                        if(p->opcode == 0xE0 && place == 1)//pitchbend is special case (14 bit number)
                        {
                            if(conditioned>16383) conditioned = 16383;
                            msg[place] += ((uint8_t)conditioned)&0x7F;
                            msg[place+1] += ((uint8_t)(conditioned/128.0));
                        }
                        else if (place > 0) // ordinary data byte
                        {
                            if(conditioned>127) conditioned = 127;
                            msg[place] += ((uint8_t)conditioned);
                        }
                        else if (p->raw_midi) // status byte
                        {
                            if(conditioned>255) conditioned = 255;
                            msg[place] += ((uint8_t)conditioned);
                        }
                        else // channel
                        {
                            if(conditioned>15) conditioned = 15;
                            msg[place] += ((uint8_t)conditioned);
                        }
                    }
                }
            }
        }
        //check if it matches range or constant
        else if(p->osc_const[i] && (v < p->osc_val[i] || v > p->osc_rangemax[i]))
        {
            //out of bounds of range or const
            return 0;
        }
        //record the value for later use in reverse mapping (MIDI->OSC) -ag
        p->regs[i] = v;
        path += n;
        //skip over the parameter value
        char *end;
        (void) strtol(path, &end, 0);
        // assert result==v && end != NULL
        path = end;
    }
    //compare the end of the path
    if(strcmp(path,p->path[i]))
    {
        return 0;
    }

    //now the actual osc args
    double val;
    for(i=0; i<p->argc; i++)
    {
        //put it in the message;
        place = p->osc_map[i+p->argc_in_path];
        if(place != -1)
        {
            switch(types[i])
            {
            case 'i':
                val = (double)argv[i]->i;
                break;
            case 'h'://long
                val = (double)argv[i]->h;
                break;
            case 'f':
                val = (double)argv[i]->f;
                break;
            case 'd':
                val = (double)argv[i]->d;
                break;
            case 'c'://char
                val = (double)argv[i]->c;
                break;
            case 'T'://true
                val = 1.0;
                break;
            case 'F'://false
                val = 0.0;
                break;
            case 'N'://nil
                val = 0.0;
                break;
            case 'I'://impulse
                val = 1.0;
                break;
            case 'm'://midi
                if(p->raw_midi && p->n==1)
                {
                    //send full midi message
                    msg[0] = argv[i]->m[1];
                    msg[1] = argv[i]->m[2];
                    msg[2] = argv[i]->m[3];
                    /* At this point, we're already done constructing the MIDI
                       message, but we still need to carry on checking all the
                       remaining arguments, to make sure that the OSC message
                       matches. -ag */
                    continue;
                }

            case 's'://string
            case 'b'://blob
            case 'S'://symbol
            case 't'://timetag
            default:
                //this isn't supported, they shouldn't use it as an arg, return error
                return 0;

            }
            for(place=0; place<p->n; place++)
            {
                if(p->midi_map[place]==i+p->argc_in_path)
                {
                    conditioned = p->midi_scale[place]*(val - p->osc_offset[i])/p->osc_scale[i] + p->midi_offset[place];
                    //check if this is a message to set global channel etc.
                    if(p->set_channel)
                    {
                        if(conditioned<0) conditioned = 0;
                        if(conditioned>15) conditioned = 15;
                        msg[place+1] = ((uint8_t)conditioned);
                    }
                    else if(p->set_velocity)
                    {
                        if(conditioned<0) conditioned = 0;
                        if(conditioned>127) conditioned = 127;
                        msg[place+1] = ((uint8_t)conditioned);
                    }
                    else if(p->set_shift)
                    {
                        msg[place+1] = ((uint8_t)conditioned);
                    }
                    //put it in the midi message
                    else if(place == 3)//only used for note on or off
                    {
                        msg[0] += ((uint8_t)(conditioned>0))<<4;
                    }
                    else
                    {
                        //clamp MIDI values
                        // - 0..255 for status bytes (arg #0 of rawmidi)
                        // - 0..15 for channel (arg #0 of other commands)
                        // - 0..127 for regular data bytes
                        // - 0..16383 for pitch bend
                        if(conditioned<0) conditioned = 0;
                        if(p->opcode == 0xE0 && place == 1)//pitchbend is special case (14 bit number)
                        {
                            if(conditioned>16383) conditioned = 16383;
                            msg[place] += ((uint8_t)conditioned)&0x7F;
                            msg[place+1] += ((uint8_t)(conditioned/128.0));
                        }
                        else if (place > 0) // ordinary data byte
                        {
                            if(conditioned>127) conditioned = 127;
                            msg[place] += ((uint8_t)conditioned);
                        }
                        else if (p->raw_midi) // status byte
                        {
                            if(conditioned>255) conditioned = 255;
                            msg[place] += ((uint8_t)conditioned);
                        }
                        else // channel
                        {
                            if(conditioned>15) conditioned = 15;
                            msg[place] += ((uint8_t)conditioned);
                        }
                    }
                }
            }
            //record the value for later use in reverse mapping -ag
            p->regs[i+p->argc_in_path] = val;
        }//if arg is used
        else
        {
            //arg not used but needs to be recorded, and we may have to check if it matches constant or range
            switch(types[i])
            {
            case 'i':
                val = (double)argv[i]->i;
                break;
            case 'h'://long
                val = (double)argv[i]->h;
                break;
            case 'f':
                val = (double)argv[i]->f;
                break;
            case 'd':
                val = (double)argv[i]->d;
                break;
            case 'c'://char
                val = (double)argv[i]->c;
                break;
            case 'T'://true
                val = 1.0;
                break;
            case 'I'://impulse
                val = 1.0;
                break;
            case 'F'://false
            case 'N'://nil
            case 'm'://midi
            case 's'://string
            case 'b'://blob
            case 'S'://symbol
            case 't'://timetag
            default:
                val = 0.0;
            }
            //check if it is in bounds of constant or range
            if(p->osc_const[i+p->argc_in_path] && (val < p->osc_val[i+p->argc_in_path] || val > p->osc_rangemax[i+p->argc_in_path]))
            {
                return 0;
            }
            //record the value for later use in reverse mapping -ag
            p->regs[i+p->argc_in_path] = val;
        }
    }//for args
    if (strict_match)
    {
        // Check for consistency of variable bindings.
        for(i=0; i<p->argc+p->argc_in_path; i++)
        {
            int j;
            place = p->osc_map[i];
            if(place != -1 && (j = p->midi_map[place]) != i)
            {
                // two different occurrences of the same variable on the lhs - check
                // that their values are the same
                float y1 = p->regs[i], y2 = p->regs[j];
                float a1 = p->osc_scale[i], a2 = p->osc_scale[j];
                float b1 = p->osc_offset[i], b2 = p->osc_offset[j];
                if ((y1-b1)*a2 != (y2-b2)*a1) return 0;
            }
        }
    }
    // Handle setchannel et al. Note that the return value -1 doesn't indicate
    // an error, but that we don't need to send a midi message (ret 0 denotes
    // error).
    if(p->set_channel)
    {
        *glob_chan = msg[1];
        return -1;
    }
    else if(p->set_velocity)
    {
        *glob_vel = msg[1];
        return -1;
    }
    else if(p->set_shift)
    {
        *filter = msg[1];
        return -1;
    }
    return 1;
}

int load_osc_value(lo_message oscm, char type, float val)
{
    switch(type)
    {
    case 'i':
        lo_message_add_int32(oscm,(int)val);
        break;
    case 'h'://long
        lo_message_add_int64(oscm,(long)val);
        break;
    case 'f':
        lo_message_add_float(oscm,val);
        break;
    case 'd':
        lo_message_add_double(oscm,(double)val);
        break;
    case 'c'://char
        lo_message_add_char(oscm,(char)val);
        break;
    case 'T'://true
        lo_message_add_true(oscm);
        break;
    case 'F'://false
        lo_message_add_false(oscm);
        break;
    case 'N'://nil
        lo_message_add_nil(oscm);
        break;
    case 'I'://impulse
        lo_message_add_infinitum(oscm);
        break;
    //all the following just load with null values
    case 'm'://midi
    {
        uint8_t m[4] = {0,0,0,0};
        lo_message_add_midi(oscm,m);
        break;
    }
    case 's'://string
        lo_message_add_string(oscm,"");//at some point may want to be able to interpret/send numbers as strings?
        break;
    case 'b'://blob
        lo_message_add_blob(oscm,0);
    case 'S'://symbol
        lo_message_add_symbol(oscm,"");
    case 't'://timetag
    {
        lo_timetag t;
        lo_timetag_now(&t);
        lo_message_add_timetag(oscm,t);
    }
    default:
        //this isn't supported, they shouldn't use it as an arg, return error
        return 0;

    }
    return 1;
}

//see if incoming midi message matches this pair and create the associated OSC message
int try_match_midi(PAIRHANDLE ph, uint8_t msg[], uint8_t strict_match, uint8_t* glob_chan, char* path, lo_message oscm)
{
    PAIR* p = (PAIR*)ph;
    uint8_t i,m[4] = {0,0,0,0}, noteon = 0;
    int8_t place;
    char chunk[100];

    //note that all noteoff messages have been converted to 0x90 opcode before this is called

    if(!p->raw_midi)
    {
        //check the opcode
        if( (msg[0]&0xF0) != p->opcode )
        {
            if( p->opcode == 0x80 && p->n == 4 && (msg[0]&0xF0) == 0x90 )
            {
                //this is actually a note() command with a variable in the 4th
                //argument, which matches a note on message
                noteon = 1;
            }
            else
            {
                return 0;
            }
        }

        //check the channel
        if(p->use_glob_chan && (msg[0]&0x0F) != *glob_chan)
        {
            return 0;
        }
        else if(p->midi_const[0] && ((msg[0]&0x0F) < p->midi_val[0] || (msg[0]&0x0F) > p->midi_rangemax[0]))
        {
            return 0;
        }

        //check the remaining arguments
        if(p->opcode == 0xE0)
        {
            //pitchbend, compare 14 bit values
            if(p->midi_const[1])
            {
                int min = p->midi_val[1]+128*p->midi_val[2];
                int max = p->midi_rangemax[1]+128*p->midi_rangemax[2];
                int val = msg[1]+128*msg[2];
                if(val < min || val > max)
                {
                    return 0;
                }
            }
        }
        //anything else, compare the individual data bytes
        else if(p->midi_const[1] && (msg[1] < p->midi_val[1] || msg[1] > p->midi_rangemax[1]))
        {
            return 0;
        }
        else if(p->midi_const[2] && (msg[2] < p->midi_val[2] || msg[2] > p->midi_rangemax[2]))
        {
            return 0;
        }

        //looks like a match, load values
        for(i=0; i<p->argc; i++)
        {
            place = p->osc_map[i+p->argc_in_path];
            if(place == 3)
            {
                load_osc_value( oscm,p->types[i],p->osc_scale[i+p->argc_in_path]*(noteon - p->midi_offset[place]) / p->midi_scale[place] + p->osc_offset[i+p->argc_in_path] );
            }
            else if(place != -1)
            {
                int midival = msg[place];
                float val;
                if(place == 0)
                {
                    //this is the status byte, actual argument is the channel
                    //number in the lo-nibble
                    midival &= 0xF;
                }
                else if(p->opcode == 0xE0 && place == 1)
                {
                    //pitchbend is special case (14 bit number)
                    midival += msg[place+1]*128;
                }
                val = p->osc_scale[i+p->argc_in_path]*((float)midival - p->midi_offset[place]) / p->midi_scale[place] + p->osc_offset[i+p->argc_in_path];
                //record the value for later use in reverse mapping -ag
                p->regs[i+p->argc_in_path] = val;
                load_osc_value( oscm,p->types[i],val );
            }
            else
            {
                // value not in message, grab default or previously recorded value -ag
                float val = p->osc_const[i+p->argc_in_path]?p->osc_val[i+p->argc_in_path]:p->regs[i+p->argc_in_path];
                load_osc_value( oscm, p->types[i], val );
            }
        }
    }
    else
    {
        //let's do a quick check here to make sure that the constant parts of
        //the MIDI message match up -ag
        for(i=0; i<p->n; i++)
        {
            if(p->midi_map[i] == -1 &&
                    (msg[i] < p->midi_val[i] || msg[i] > p->midi_rangemax[i]))
                return 0;
        }
        for(i=0; i<p->argc; i++)
        {
            place = p->osc_map[i+p->argc_in_path];
            if(p->types[i] == 'm' && place != -1)
            {
                if (p->n!=1)
                    return 0; // this is only supported for midimessage()
                m[1] = msg[0];
                m[2] = msg[1];
                m[3] = msg[2];
                m[0] = 0;//port ID
                lo_message_add_midi(oscm,m);
            }
            else if(place != -1)
            {
                int midival = msg[place];
                float val = p->osc_scale[i+p->argc_in_path]*((float)midival - p->midi_offset[place]) / p->midi_scale[place] + p->osc_offset[i+p->argc_in_path];
                //record the value for later use in reverse mapping -ag
                p->regs[i+p->argc_in_path] = val;
                load_osc_value( oscm,p->types[i],val );
            }
            else
            {
                //we have no idea what should be in these, so just load a previously recorded value or the defaults
                float val = p->osc_const[i+p->argc_in_path]?p->osc_val[i+p->argc_in_path]:p->regs[i+p->argc_in_path];
                load_osc_value( oscm, p->types[i], val );
            }
        }
    }

    //now the path
    path[0] = 0;
    for(i=0; i<p->argc_in_path; i++)
    {
        place = p->osc_map[i];
        if(place == 3)
        {
            sprintf(chunk,p->path[i], (int)(p->osc_scale[i]*(noteon - p->midi_offset[place]) / p->midi_scale[place] + p->osc_offset[i]));
        }
        else if(place != -1)
        {
            int midival = msg[place];
            float val;
            if(!p->raw_midi && place == 0)
            {
                //this is the status byte, actual argument is the channel
                //number in the lo-nibble
                midival &= 0xF;
            }
            else if(p->opcode == 0xE0 && place == 1)
            {
                //pitchbend is special case (14 bit number)
                midival += msg[place+1]*128;
            }
            val = p->osc_scale[i]*(midival - p->midi_offset[place]) / p->midi_scale[place] + p->osc_offset[i];
            //record the value for later use in reverse mapping -ag
            p->regs[i] = val;
            sprintf(chunk, p->path[i], (int)val);
        }
        else
        {
            // value not in message, grab default or previously recorded value -ag
            float val = p->osc_const[i]?p->osc_val[i]:p->regs[i];
            sprintf(chunk, p->path[i], (int)val);
        }
        strcat(path, chunk);
    }
    strcat(path, p->path[i]);
    if (strict_match)
    {
        // Check for consistency of variable bindings.
        for(i=0; i<p->n; i++)
        {
            int j;
            place = p->midi_map[i];
            if(place != -1 && (j = p->osc_map[place]) != i)
            {
                // two different occurrences of the same variable on the rhs - check
                // that their values are the same
                uint8_t y1 = msg[i], y2 = msg[j];
                float a1 = p->midi_scale[i], a2 = p->midi_scale[j];
                float b1 = p->midi_offset[i], b2 = p->midi_offset[j];
                if (!p->raw_midi)
                {
                    if (i==0) y1 &= 0xF;
                    if (j==0) y2 &= 0xF;
                }
                // give some leeway here to account for the rounding of MIDI arguments
                if (y1 != ((int)((y2-b2)*a1/a2+b1))) return 0;
            }
        }
    }

    return 1;
}

char * opcode2cmd(uint8_t opcode, uint8_t noteoff)
{
    switch(opcode)
    {
    case 0x90:
    case 0x91:
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0x97:
    case 0x98:
    case 0x99:
    case 0x9A:
    case 0x9B:
    case 0x9C:
    case 0x9D:
    case 0x9E:
    case 0x9F:
        return "noteon";
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0x85:
    case 0x86:
    case 0x87:
    case 0x88:
    case 0x89:
    case 0x8A:
    case 0x8B:
    case 0x8C:
    case 0x8D:
    case 0x8E:
    case 0x8F:
        if(noteoff) return "noteoff";
        return "note";
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA3:
    case 0xA4:
    case 0xA5:
    case 0xA6:
    case 0xA7:
    case 0xA8:
    case 0xA9:
    case 0xAA:
    case 0xAB:
    case 0xAC:
    case 0xAD:
    case 0xAE:
    case 0xAF:
        return "polyaftertouch";
    case 0xB0:
    case 0xB1:
    case 0xB2:
    case 0xB3:
    case 0xB4:
    case 0xB5:
    case 0xB6:
    case 0xB7:
    case 0xB8:
    case 0xB9:
    case 0xBA:
    case 0xBB:
    case 0xBC:
    case 0xBD:
    case 0xBE:
    case 0xBF:
        return "controlchange";
    case 0xC0:
    case 0xC1:
    case 0xC2:
    case 0xC3:
    case 0xC4:
    case 0xC5:
    case 0xC6:
    case 0xC7:
    case 0xC8:
    case 0xC9:
    case 0xCA:
    case 0xCB:
    case 0xCC:
    case 0xCD:
    case 0xCE:
    case 0xCF:
        return "programchange";
    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3:
    case 0xD4:
    case 0xD5:
    case 0xD6:
    case 0xD7:
    case 0xD8:
    case 0xD9:
    case 0xDA:
    case 0xDB:
    case 0xDC:
    case 0xDD:
    case 0xDE:
    case 0xDF:
        return "aftertouch";
    case 0xE0:
    case 0xE1:
    case 0xE2:
    case 0xE3:
    case 0xE4:
    case 0xE5:
    case 0xE6:
    case 0xE7:
    case 0xE8:
    case 0xE9:
    case 0xEA:
    case 0xEB:
    case 0xEC:
    case 0xED:
    case 0xEE:
    case 0xEF:
        return "pitchbend";
    case 0x00:
        return "rawmidi";
    case 0x01:
        return "midimessage";
    case 0x02:
        return "setchannel";
    case 0x03:
        return "setvelocity";
    case 0x04:
        return "setshift";
    default:
        return "unknown";
    }
}

void print_midi(PAIRHANDLE ph, uint8_t msg[])
{
    PAIR* p = (PAIR*)ph;
    int status = msg[0]&0xf0;
    if(p->raw_midi) // this needs special treatment
        printf("%s ( %i, %i, %i )", opcode2cmd(p->opcode,1), msg[0], msg[1], msg[2]);
    else if (status == 0xc0 || status == 0xd0)
    {
        // single data byte
        printf("%s ( %i, %i )", opcode2cmd(msg[0],1), msg[0]&0x0F, msg[1]);
    }
    else if (status == 0xe0)
    {
        // pitch bend
        printf("%s ( %i, %i )", opcode2cmd(msg[0],1), msg[0]&0x0F, msg[1]+128*msg[2]);
    }
    else
    {
        // anything else should have two data bytes
        printf("%s ( %i, %i, %i )", opcode2cmd(msg[0],1), msg[0]&0x0F, msg[1], msg[2]);
    }
}
