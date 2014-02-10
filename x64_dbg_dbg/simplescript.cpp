#include "simplescript.h"
#include "value.h"
#include "console.h"
#include "argument.h"
#include "variable.h"

static std::vector<LINEMAPENTRY> linemap;
static std::vector<SCRIPTBP> scriptbplist;
static int scriptIp=0;
static bool bAbort=false;
static bool bIsRunning=false;

static SCRIPTBRANCHTYPE scriptgetbranchtype(const char* text)
{
    if(!strncmp(text, "jmp", 3) or !strncmp(text, "goto", 4))
        return scriptjmp;
    else if(!strncmp(text, "jne", 3) or !strncmp(text, "ifne", 4) or !strncmp(text, "ifneq", 5) or !strncmp(text, "jnz", 3) or !strncmp(text, "ifnz", 4))
        return scriptjnejnz;
    else if(!strncmp(text, "je", 2)  or !strncmp(text, "ife", 3) or !strncmp(text, "ifeq", 4) or !strncmp(text, "jz", 2) or !strncmp(text, "ifz", 3))
        return scriptjejz;
    else if(!strncmp(text, "jb", 2) or !strncmp(text, "ifb", 3) or !strncmp(text, "jl", 2) or !strncmp(text, "ifl", 3))
        return scriptjbjl;
    else if(!strncmp(text, "ja", 2) or !strncmp(text, "ifa", 3) or !strncmp(text, "jg", 2) or !strncmp(text, "ifg", 3))
        return scriptjajg;
    else if(!strncmp(text, "jbe", 3) or !strncmp(text, "ifbe", 4) or !strncmp(text, "ifbeq", 5) or !strncmp(text, "jle", 3) or !strncmp(text, "ifle", 4) or !strncmp(text, "ifleq", 5))
        return scriptjbejle;
    else if(!strncmp(text, "jae", 3) or !strncmp(text, "ifae", 4) or !strncmp(text, "ifaeq", 5) or !strncmp(text, "jge", 3) or !strncmp(text, "ifge", 4) or !strncmp(text, "ifgeq", 5))
        return scriptjaejge;
    return scriptnobranch;
}

static bool scriptislabel(const char* text)
{
    if(!strstr(text, " ") and !strstr(text, ",") and !strstr(text, "\\") and text[strlen(text)-1]==':')
        return true;
    return false;
}

static int scriptlabelfind(const char* labelname)
{
    int linecount=linemap.size();
    for(int i=0; i<linecount; i++)
        if(linemap.at(i).type==linelabel && !strcmp(linemap.at(i).u.label, labelname))
            return i+1;
    return 0;
}

static bool scriptcreatelinemap(const char* filename)
{
    HANDLE hFile=CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if(hFile==INVALID_HANDLE_VALUE)
    {
        GuiScriptError(0, "CreateFile failed...");
        return false;
    }
    unsigned int filesize=GetFileSize(hFile, 0);
    char* filedata=(char*)emalloc(filesize+1, "createlinemap:filedata");
    memset(filedata, 0, filesize+1);
    DWORD read=0;
    if(!ReadFile(hFile, filedata, filesize, &read, 0))
    {
        CloseHandle(hFile);
        GuiScriptError(0, "ReadFile failed...");
        efree(filedata, "createlinemap:filedata");
        return false;
    }
    CloseHandle(hFile);
    int len=strlen(filedata);
    char temp[256]="";
    LINEMAPENTRY entry;
    memset(&entry, 0, sizeof(entry));
    linemap.clear();
    for(int i=0,j=0; i<len; i++) //make raw line map
    {
        if(filedata[i]=='\r' and filedata[i+1]=='\n') //windows file
        {
            memset(&entry, 0, sizeof(entry));
            strcpy(entry.raw, temp);
            *temp=0;
            j=0;
            i++;
            linemap.push_back(entry);
        }
        else if(filedata[i]=='\n') //other file
        {
            memset(&entry, 0, sizeof(entry));
            strcpy(entry.raw, temp);
            *temp=0;
            j=0;
            linemap.push_back(entry);
        }
        else if(j>=254)
        {
            memset(&entry, 0, sizeof(entry));
            argformat(temp);
            strcpy(entry.raw, temp);
            *temp=0;
            j=0;
            linemap.push_back(entry);
        }
        else
            j+=sprintf(temp+j, "%c", filedata[i]);
    }
    if(*temp)
    {
        memset(&entry, 0, sizeof(entry));
        strcpy(entry.raw, temp);
        linemap.push_back(entry);
    }
    efree(filedata, "createlinemap:filedata");
    unsigned int linemapsize=linemap.size();
    while(!*linemap.at(linemapsize-1).raw) //remove empty lines
    {
        linemapsize--;
        linemap.pop_back();
    }
    for(unsigned int i=0; i<linemapsize; i++)
    {
        LINEMAPENTRY cur=linemap.at(i);
        int rawlen=strlen(cur.raw);
        if(!strlen(cur.raw)) //empty
        {
            cur.type=lineempty;
        }
        else if(!strncmp(cur.raw, "//", 2)) //comment
        {
            cur.type=linecomment;
            strcpy(cur.u.comment, cur.raw);
        }
        else if(scriptislabel(cur.raw)) //label
        {
            cur.type=linelabel;
            strncpy(cur.u.label, cur.raw, rawlen-1);
            int foundlabel=scriptlabelfind(cur.u.label);
            if(foundlabel) //label defined twice
            {
                char message[256]="";
                sprintf(message, "Duplicate label \"%s\" detected on lines %d and %d!", cur.u.label, foundlabel, i+1);
                GuiScriptError(0, message);
                linemap.clear();
                return false;
            }
        }
        else if(scriptgetbranchtype(cur.raw)!=scriptnobranch) //branch
        {
            cur.type=linebranch;
            cur.u.branch.type=scriptgetbranchtype(cur.raw);
            int len=strlen(cur.raw);
            for(int i=0; i<len; i++)
                if(cur.raw[i]==' ')
                {
                    strcpy(cur.u.branch.branchlabel, cur.raw+i+1);
                    break;
                }
        }
        else
        {
            cur.type=linecommand;
            strcpy(cur.u.command, cur.raw);
        }
        linemap.at(i)=cur;
    }
    linemapsize=linemap.size();
    for(unsigned int i=0; i<linemapsize; i++)
    {
        LINEMAPENTRY cur=linemap.at(i);
        if(cur.type==linebranch and !scriptlabelfind(cur.u.branch.branchlabel)) //invalid branch label
        {
            char message[256]="";
            sprintf(message, "Invalid branch label \"%s\" detected on line %d!", cur.u.branch.branchlabel, i+1);
            GuiScriptError(0, message);
            linemap.clear();
            return false;
        }
    }
    if(linemap.at(linemapsize-1).type==linecomment or linemap.at(linemapsize-1).type==linelabel) //label/comment on the end
    {
        memset(&entry, 0, sizeof(entry));
        entry.type=linecommand;
        strcpy(entry.raw, "ret");
        strcpy(entry.u.command, "ret");
        linemap.push_back(entry);
    }
    return true;
}

static int scriptinternalstep(int fromIp) //internal step routine
{
    int maxIp=linemap.size(); //maximum ip
    if(fromIp>=maxIp) //script end
        return fromIp;
    while((linemap.at(fromIp).type==lineempty or linemap.at(fromIp).type==linecomment or linemap.at(fromIp).type==linelabel) and fromIp<maxIp) //skip empty lines
        fromIp++;
    fromIp++;
    return fromIp;
}

static bool scriptinternalbpget(int line) //internal bpget routine
{
    int bpcount=scriptbplist.size();
    for(int i=0; i<bpcount; i++)
        if(scriptbplist.at(i).line==line)
            return true;
    return false;
}

static bool scriptinternalbptoggle(int line) //internal breakpoint
{
    if(!line or line>(int)linemap.size()) //invalid line
        return false;
    line=scriptinternalstep(line-1); //no breakpoints on non-executable locations
    if(scriptinternalbpget(line)) //remove breakpoint
    {
        int bpcount=scriptbplist.size();
        for(int i=0; i<bpcount; i++)
            if(scriptbplist.at(i).line==line)
            {
                scriptbplist.erase(scriptbplist.begin()+i);
                break;
            }
    }
    else //add breakpoint
    {
        SCRIPTBP newbp;
        newbp.silent=true;
        newbp.line=line;
        scriptbplist.push_back(newbp);
    }
    return true;
}

static CMDRESULT scriptinternalcmdexec(const char* command)
{
    dprintf("scriptinternalcmdexec(%s)\n", command);
    if(!strcmp(command, "ret"))
    {
        GuiScriptMessage("Script finished!");
        return STATUS_EXIT;
    }
    else if(!strcmp(command, "invalid"))
        return STATUS_ERROR;
    return STATUS_CONTINUE;
}

static bool scriptinternalbranch(SCRIPTBRANCHTYPE type) //determine if we should jump
{
    uint ezflag=0;
    uint bsflag=0;
    varget("$_EZ_FLAG", &ezflag, 0, 0);
    varget("$_BS_FLAG", &bsflag, 0, 0);
    bool bJump=false;
    switch(type)
    {
    case scriptjmp:
        bJump=true;
        break;
    case scriptjnejnz: //$_EZ_FLAG=0
        if(!ezflag)
            bJump=true;
        break;
    case scriptjejz: //$_EZ_FLAG=1
        if(ezflag)
            bJump=true;
        break;
    case scriptjbjl: //$_BS_FLAG=0 and $_EZ_FLAG=0 //below, not equal
        if(!bsflag and !ezflag)
            bJump=true;
        break;
    case scriptjajg: //$_BS_FLAG=1 and $_EZ_FLAG=0 //above, not equal
        if(bsflag and !ezflag)
            bJump=true;
        break;
    case scriptjbejle: //$_BS_FLAG=0 or $_EZ_FLAG=1
        if(!bsflag or ezflag)
            bJump=true;
        break;
    case scriptjaejge: //$_BS_FLAG=1 or $_EZ_FLAG=1
        if(bsflag or ezflag)
            bJump=true;
        break;
    default:
        bJump=false;
        break;
    }
    return bJump;
}

static bool scriptinternalcmd()
{
    bool bContinue=true;
    LINEMAPENTRY cur=linemap.at(scriptIp-1);
    if(cur.type==linecommand)
    {
        switch(scriptinternalcmdexec(cur.u.command))
        {
        case STATUS_CONTINUE:
            break;
        case STATUS_ERROR:
            bContinue=false;
            GuiScriptError(scriptIp, "Error executing command!");
            break;
        case STATUS_EXIT:
            bContinue=false;
            scriptIp=scriptinternalstep(0);
            GuiScriptSetIp(scriptIp);
            break;
        }
    }
    else if(cur.type==linebranch and scriptinternalbranch(cur.u.branch.type)) //branch
        scriptIp=scriptlabelfind(cur.u.branch.branchlabel);
    return bContinue;
}

static DWORD WINAPI scriptRunThread(void* arg)
{
    int destline=(int)(uint)arg;
    if(!destline or destline>(int)linemap.size()) //invalid line
        destline=0;
    if(destline)
    {
        destline=scriptinternalstep(destline-1); //no breakpoints on non-executable locations
        if(!scriptinternalbpget(destline)) //no breakpoint set
            scriptinternalbptoggle(destline);
    }
    bAbort=false;
    if(scriptIp)
        scriptIp--;
    scriptIp=scriptinternalstep(scriptIp);
    bool bContinue=true;
    while(bContinue && !bAbort) //run loop
    {
        bContinue=scriptinternalcmd();
        if(scriptIp==scriptinternalstep(scriptIp)) //end of script
        {
            bContinue=false;
            scriptIp=scriptinternalstep(0);
        }
        if(bContinue)
            scriptIp=scriptinternalstep(scriptIp); //this is the next ip
        if(scriptinternalbpget(scriptIp)) //breakpoint=stop run loop
            bContinue=false;
        Sleep(1); //don't fry the processor
    }
    bIsRunning=false; //not running anymore
    GuiScriptSetIp(scriptIp);
    return 0;
}

void scriptload(const char* filename)
{
    GuiScriptClear();
    scriptIp=0;
    scriptbplist.clear(); //clear breakpoints
    bAbort=false;
    if(!scriptcreatelinemap(filename))
        return;
    for(unsigned int i=0; i<linemap.size(); i++) //add script lines
        GuiScriptAddLine(linemap.at(i).raw);
    scriptIp=scriptinternalstep(0);
    GuiScriptSetIp(scriptIp);
}

void scriptunload()
{
    GuiScriptClear();
    scriptIp=0;
    scriptbplist.clear(); //clear breakpoints
    bAbort=false;
}

void scriptrun(int destline)
{
    if(bIsRunning) //already running
        return;
    bIsRunning=true;
    CreateThread(0, 0, scriptRunThread, (void*)(uint)destline, 0, 0);
}

void scriptstep()
{
    if(bIsRunning) //already running
        return;
    scriptIp=scriptinternalstep(scriptIp-1); //probably useless
    if(!scriptinternalcmd())
        return;
    if(scriptIp==scriptinternalstep(scriptIp)) //end of script
        scriptIp=0;
    scriptIp=scriptinternalstep(scriptIp);
    GuiScriptSetIp(scriptIp);
}

bool scriptbptoggle(int line)
{
    if(!line or line>(int)linemap.size()) //invalid line
        return false;
    line=scriptinternalstep(line-1); //no breakpoints on non-executable locations
    if(scriptbpget(line)) //remove breakpoint
    {
        int bpcount=scriptbplist.size();
        for(int i=0; i<bpcount; i++)
            if(scriptbplist.at(i).line==line && !scriptbplist.at(i).silent)
            {
                scriptbplist.erase(scriptbplist.begin()+i);
                break;
            }
    }
    else //add breakpoint
    {
        SCRIPTBP newbp;
        newbp.silent=false;
        newbp.line=line;
        scriptbplist.push_back(newbp);
    }
    return true;
}

bool scriptbpget(int line)
{
    int bpcount=scriptbplist.size();
    for(int i=0; i<bpcount; i++)
        if(scriptbplist.at(i).line==line && !scriptbplist.at(i).silent)
            return true;
    return false;
}

bool scriptcmdexec(const char* command)
{
    switch(scriptinternalcmdexec(command))
    {
    case STATUS_ERROR:
        return false;
        break;
    case STATUS_EXIT:
        scriptIp=scriptinternalstep(0);
        GuiScriptSetIp(scriptIp);
        return true;
        break;
    case STATUS_CONTINUE:
        break;
    }
    return true;
}

void scriptabort()
{
    if(bIsRunning)
        bAbort=true;
}

SCRIPTLINETYPE scriptgetlinetype(int line)
{
    if(line>(int)linemap.size())
        return lineempty;
    return linemap.at(line-1).type;
}

void scriptsetip(int line)
{
    if(line)
        line--;
    scriptIp=scriptinternalstep(line);
    GuiScriptSetIp(scriptIp);
}
