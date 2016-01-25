/*_______________________________________________________________
 |                                                               |
 | Joy2TX Win32, Version 2.3                                     |
 | Copyright (c) 2000-2014 by Antoni Sawicki <as@tenoware.com>   |
 | License: MIT                                                  |
 |                                                               |
 | Supported devices:                                            |
 |   - PCBUDDY by RC-ELECTRONICS [Originally by Ken Hewitt]      |
 |   - SC8000 by Tom's RC [Developed for TCNI]                   |
 |_______________________________________________________________|
 |
 |                                                               
 |
 */          

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#define VERSION "2.3"
#define DEFCFGFILE "joy2tx.cfg"
#define CHANNELS 8
#define DEFINTERVAL 30         
#define BUFFSIZE 1024
#define CH_OFF 0
#define CH_JOY 1
#define CH_FIX 2
#define CH_BUT 3
#define DEV_PCBUDDY 0
#define DEV_SC8000 1
#define HI 1
#define LO 0

struct chdef {
    int def;
    int fixed;
    int joy;
    char axis;
    int data;
};

char *DEVNAME[2] = { "PCBUDDY", "SC8000" };
int  DEVCHMIN[2] = { 0, 8000 };
int  DEVCHMAX[2] = { 250, 22000 };

int DBG=0;
int COMMS=1;

int main(int argc, char **argv) {
    // general
    char buff[BUFFSIZE];
    FILE *cfgfile; 
    char cfgfilename[128];
    LARGE_INTEGER t_ps, t_start, t_end;
    int interval=DEFINTERVAL;
    int wchs=0;
    int errcnt=0;

    // console
    HANDLE cons;
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    COORD cursor_home = {0,3}, cursor_zero = {0,0};
    char errmsg[128];

    // device
    char devname[10];
    int dev=-1, dvcs=0;
    int emitlen=0;
    
    // channel
    int i, j, chs=0;
    char c;
    struct chdef ch[CHANNELS+1]; // ch[0] will be unused
    unsigned int sc8000_chmask=0;
    unsigned char *p;
    
    // joystick
    JOYINFOEX joydata;
    JOYCAPS joycaps;

    // comm port
    char comname[6];
    int comno;
    HANDLE *comhandle;
    DCB dcb;
    
    //
    // start...
    //
    SetConsoleTitle("Joy2TX %s by Antoni Sawicki <as@tenoware.com>", VERSION);

    _snprintf(cfgfilename, 128, "%s", DEFCFGFILE);

    joydata.dwSize=sizeof(JOYINFOEX);
    joydata.dwFlags=0;
    
    t_ps.QuadPart=0;
    t_start.QuadPart=0;
    t_end.QuadPart=0;


    for(i=0;i<=CHANNELS;i++)
        ch[i].def=CH_OFF;

    
    //
    // parse cmd args
    //
    if(argc>1) {
        if(strcmp(argv[1], "-d")==0) {
            DBG=1;
        }
        else if(strcmp(argv[1], "-v")==0) {
            printf("Joy2TX Win32, Version %s, Build: %s/%s\n"
                   "Copyright (c) 2003-2014 by Antoni Sawicki <as@tenoware.com>\n"
                   "Website: http://www.tenox.net/out#joy2tx\n", VERSION, __DATE__, __TIME__);
            printf("\nCredits:\n Original concept [PC to RC] by Ken Hewitt\n");
            printf(" PCBUDDY hw by RC-ELECTRONICS http://www.rc-electronics.co.uk/\n");
            printf(" SC8000 hw by Tom's RC http://www.tti-us.com/rc/projects.htm\n");
            printf(" SC8000 driver developed for TCNI http://www.tcni.com/\n");
            
            return 0;
        }
        else if(strcmp(argv[1], "-j")==0) {
            printf("Enumerating joysticks:\n");
            for(i=0; i<=joyGetNumDevs(); i++) {
                if(joyGetDevCaps(i, &joycaps, sizeof(JOYCAPS))==JOYERR_NOERROR) {
                    printf("%2d: %s with %d axes: [", i, joycaps.szPname, joycaps.wNumAxes);
                    if(joycaps.wNumAxes>=2) {
                        printf("x,y");
                        if(joycaps.wCaps & JOYCAPS_HASV)
                            printf(",v");
                        if(joycaps.wCaps & JOYCAPS_HASR)
                            printf(",r");
                        if(joycaps.wCaps & JOYCAPS_HASZ)
                            printf(",z");
                        if(joycaps.wCaps & JOYCAPS_HASU)
                            printf(",u");
                    }
                    printf("] and %d buttons\n", joycaps.wNumButtons);
                    dvcs++;
                }
            }
            
            printf("\n%d device%s found\n", dvcs, (dvcs==1) ? "" : "s");
            return 0;
        }
        else if(strcmp(argv[1], "-n")==0) {
            printf("Comms disabled\n");
            COMMS=0;
        }
        else if(argc>2 && (strcmp(argv[1], "-f")==0 && strlen(argv[2])>5)) {
            _snprintf(cfgfilename, 128, "%s", argv[2]);
        }
        else {
            printf("usage: joy2tx.exe [-d|-v|-j|-n|-f filename]\n       -d enable debug output\n       -v display version info\n       -j list attached joysticks\n       -n disable output (comms)\n       -f alternate config filename\n");
            return 1;
        }
    }

    //
    // do stuff
    //    
    if(DBG) {
        printf("DBG: Joy2TX Win32, Version %s, Build: %s/%s\nDBG: Copyright (c) 2003-2014 by Antoni Sawicki <as@tenoware.com>\n", VERSION, __DATE__, __TIME__);
        if(QueryPerformanceFrequency(&t_ps)!=0) 
            printf("DBG: Timer resolution: %I64d\n", t_ps.QuadPart);
        else
            printf("DBG: No timer resolution available\n");
    }
    
    //
    // read config file
    //
    if(DBG) printf("DBG: Reading configuration file %s\n", cfgfilename);
    
    cfgfile=fopen(cfgfilename, "r");
    if(!cfgfile) {
        fprintf(stderr, "ERROR: unable to open configuration file %s\n", cfgfilename);
        return 1;
    }
    
    while(fgets(buff, BUFFSIZE, cfgfile)) {
        i=0; j=0; c=0; // scanf params

        if((*buff==';')||(*buff==' ')||(*buff=='#')||(*buff=='\n')) 
            continue;
        
        // define device type
        else if(sscanf(buff, "device=com%d:%9s", &comno, devname)==2) {
            _snprintf(comname, 6, "COM%d", comno);
            if(DBG) printf("DBG: def device[%s]=[%s]\n", comname, devname);
            
            if(dvcs) {
                fprintf(stderr, "ERROR: device already defined [%s]\n", DEVNAME[dev]);
                return 1;
            }
            
            if(COMMS) {
                comhandle=CreateFile(comname, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
                if((comhandle==INVALID_HANDLE_VALUE) || (!GetCommState(comhandle, &dcb))) {
                    fprintf(stderr, "ERROR: unable to open %s\n", comname);
                    return 1;
                }
                if(DBG) printf("DBG: port [%s] seems usable\n", comname);
            }
            
            if(stricmp(devname, "pcbuddy")==0) { 
                dev=DEV_PCBUDDY;
            }
            else if(stricmp(devname, "sc8000")==0) {
                dev=DEV_SC8000;
            }
            else {
                fprintf(stderr, "ERROR: device [%s] can be either 'pcbuddy' or 'sc8000'\n", devname);
                return 1;
            }
            
            dvcs++;
            continue;
        }

        // interval
        else if(sscanf(buff, "interval=%d", &i)==1) {
            if(i<10) {
                fprintf(stderr, "ERROR: interval %d too low\n", i);
                return 1;
            }
            if(DBG) printf("DBG: def interval=%d ms\n", i);
            interval=i;
            continue;
        }

        // options
        else if(strncmp(buff, "option=nocomms", 14)==0) {
            if(DBG) printf("DBG: COMMS DISABLED\n");
            COMMS=0;
            continue;
        }
        else if(strncmp(buff, "option=debug", 12)==0) {
            printf("DBG: DEBUG ENABLED\n");
            DBG=1;
            continue;
        }
        else if(strncmp(buff, "option=deadzone", 15)==0) {
            printf("DBG: deadzone enabled\n");
            joydata.dwFlags |= JOY_USEDEADZONE;
            continue;
        }
    
        // joystick axis input
        else if(sscanf(buff, "ch%1d=j%1d:%1c", &i, &j, &c)==3) {
            if(DBG) printf("DBG: def channel[%d]=joystick[%d]:axis[%c]\n", i, j, c);
            if(!dvcs) {
                fprintf(stderr, "ERROR: channel definition but no device defined\n");
                return 1;
            }
            if((i<1)||(i>CHANNELS)) {
                fprintf(stderr, "ERROR: channel %d out of range (1-%d)\n", i, CHANNELS);
                return 1;
            }
            if(ch[i].def) {
                fprintf(stderr, "ERROR: channel %d already defined\n", i);
                return 1;
            }
            if (joyGetDevCaps(j, &joycaps, sizeof(JOYCAPS))!=JOYERR_NOERROR) {
                fprintf(stderr, "ERROR: joystick %d not operable\n", j);
                return 1;
            } 
            if(c!='x'&&c!='X'&&c!='y'&&c!='Y'&&c!='z'&&c!='Z'&&c!='u'&&c!='U'&&c!='v'&&c!='V'&&c!='r'&&c!='R') {
                fprintf(stderr, "ERROR: axis can be only one of [xX,yY,zZ,rR,uU,vV]\n");
                return 1;
            }
            if(c=='x' || c=='X' || c=='y' || c=='Y') {
                if(joycaps.wNumAxes<2) {
                    fprintf(stderr, "ERROR: axis %c not supported by joystick %d\n", c, j);
                    return 1;
                } else {               
                    joydata.dwFlags|=JOY_RETURNX;
                    joydata.dwFlags|=JOY_RETURNY;
                }
            } 
            if(c=='z' || c=='Z') {
                if(joycaps.wCaps&JOYCAPS_HASZ) {
                    joydata.dwFlags|=JOY_RETURNZ;
                }
                else {
                    fprintf(stderr, "ERROR: axis %c not supported by joystick %d\n", c, j);
                    return 1;
                } 
            } 
            if(c=='r' || c=='R') {
                if(joycaps.wCaps&JOYCAPS_HASR) {
                    joydata.dwFlags|=JOY_RETURNR;
                }
                else {
                    fprintf(stderr, "ERROR: axis %c not supported by joystick %d\n", c, j);
                    return 1;
                } 
            } 
            if(c=='u' || c=='U') {
                if(joycaps.wCaps&JOYCAPS_HASU) {
                    joydata.dwFlags|=JOY_RETURNU;
                }
                else {
                    fprintf(stderr, "ERROR: axis %c not supported by joystick %d\n", c, j);
                    return 1;
                } 
            } 
            if(c=='v' || c=='V') {
                if(joycaps.wCaps&JOYCAPS_HASV) {
                    joydata.dwFlags|=JOY_RETURNV;
                }
                else {
                    fprintf(stderr, "ERROR: axis %c not supported by joystick %d\n", c, j);
                    return 1;
                } 
            } 
            
            ch[i].def=CH_JOY;
            ch[i].joy=j;
            ch[i].axis=c;
            chs++;    
            continue;      
        } 
        
        
        // fixed type        
        else if(sscanf(buff, "ch%1d=fixed:%d", &i, &j)==2) {
            if(DBG) printf("DBG: def channel[%d]=fixed_value[%d]\n", i, j);
            if(!dvcs) {
                fprintf(stderr, "ERROR: channel definition but no device defined\n");
                return 1;
            }
            if((i<1)||(i>CHANNELS)) {
                fprintf(stderr, "ERROR: channel %d out of range (1-%d)\n", i, CHANNELS);
                return 1;
            }
            if(ch[i].def) {
                fprintf(stderr, "ERROR: channel %d already defined\n", i);
                return 1;
            }
            if(j<DEVCHMIN[dev] || j>DEVCHMAX[dev]) {
                fprintf(stderr, "ERROR: ch[%d] range for [%s] is [%d-%d]\n", i, DEVNAME[dev], DEVCHMIN[dev], DEVCHMAX[dev]);
                return 1;
            }
            ch[i].def=CH_FIX;
            ch[i].fixed=j;
            chs++;
            continue;
        }

        // joystick button input
        else if(sscanf(buff, "ch%1d=button", &i)==1) {
            if(DBG) printf("DBG: def channel[%d]=button\n", i);
            joydata.dwFlags|=JOY_RETURNBUTTONS;
            ch[i].def=CH_BUT;
            chs++;
        }
        
        // force off
        else if(sscanf(buff, "ch%1d=off", &i)==1) {
            if(DBG) printf("DBG: def channel[%d]=forced_off\n", i);
            ch[i].def=CH_OFF;
            // does not increment chs
            continue;
        }

        
        else {
            fprintf(stderr, "ERROR: unknown statement [%s]\n", buff);
            return 1;
        }
    }
    fclose(cfgfile);

    if(!dvcs) {
        fprintf(stderr, "ERROR: device not set\n");
        return 1;
    }
    
    if(!chs) {
        fprintf(stderr, "ERROR: no channels defined\n");
        return 1;
    }
    
    if(DBG) {
        printf("DBG: %d device and %d channels defined\n", dvcs, chs);
        printf("DBG: joyflags=(%d) ", joydata.dwFlags);
        for (i=0;i<16;i++) 
            putchar((joydata.dwFlags & (1<<i)) ? '1' : '0');
        putchar('\n');

        printf("DBG: press Enter to continue...");
        getchar();
    }
        
        
    //
    // initialize comms
    //
    if(COMMS) {
        if(!GetCommState(comhandle, &dcb)) {
            fprintf(stderr, "I/O Error: Unable to retreive %s: state.\n", comname);
            return 1;
        }
    
        if(dev==DEV_SC8000) {
            dcb.BaudRate = CBR_9600;     // 9600 BPS
            dcb.ByteSize = 8;            // 8
            dcb.Parity = NOPARITY;       // N
            dcb.StopBits = ONESTOPBIT;   // 1
            dcb.fParity = NOPARITY;
            dcb.fOutxCtsFlow = TRUE;     // enable CTS transmit handshaking
            dcb.fOutxDsrFlow = FALSE;
            dcb.fDtrControl = DTR_CONTROL_ENABLE; // DTR disabled, but allows controll lines
            dcb.fDsrSensitivity = FALSE;
            dcb.fTXContinueOnXoff = TRUE; //
            dcb.fOutX = FALSE;            // disable xon/xoff completely
            dcb.fInX = FALSE;             //
            dcb.fErrorChar = FALSE;
            dcb.fNull = FALSE;
            dcb.fBinary = TRUE;
            dcb.fRtsControl = RTS_CONTROL_ENABLE; // RTS enabled but no handshake & allow controll lines
            dcb.fAbortOnError = FALSE;    // continue on error...(there should be error handling implemented instead...)
        }
        else if(dev==DEV_PCBUDDY) {
            dcb.BaudRate = CBR_19200;    // 19200 BPS
            dcb.ByteSize = 8;            // 8
            dcb.Parity = NOPARITY;       // N
            dcb.StopBits = ONESTOPBIT;   // 1
            dcb.fParity = NOPARITY;
            dcb.fOutxCtsFlow = TRUE;     // enable CTS transmit handshaking
            dcb.fOutxDsrFlow = FALSE;
            dcb.fDtrControl = DTR_CONTROL_ENABLE; // DTR disabled, but allows controll lines
            dcb.fDsrSensitivity = FALSE;
            dcb.fTXContinueOnXoff = TRUE; //
            dcb.fOutX = FALSE;            // disable xon/xoff completely
            dcb.fInX = FALSE;             //
            dcb.fErrorChar = FALSE;
            dcb.fNull = FALSE;
            dcb.fBinary = TRUE;
            dcb.fRtsControl = RTS_CONTROL_ENABLE; // RTS enabled but no handshake & allow controll lines
            dcb.fAbortOnError = FALSE;    // continue on error...(there should be error handling implemented instead...)    
        }
    
        if(!SetCommState(comhandle, &dcb)) {
            fprintf(stderr, "I/O Error: Unable to set communication parameters for %s:.\n", comname);
            return 1;
        }
    
        EscapeCommFunction(comhandle, CLRBREAK);
    }
    
    //
    // initialize device
    //
    if(COMMS) {
        if(dev==DEV_PCBUDDY) {
            // reset pcbuddy (it's powered by RTS controll line)
            EscapeCommFunction(comhandle, CLRRTS); // off
            Sleep(100);
            EscapeCommFunction(comhandle, SETRTS); // on
            Sleep(100);
        }
    }
    
    //
    // initialize console
    //
    cons = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(cons, &console_info);
    FillConsoleOutputCharacter(cons, ' ', console_info.dwSize.X * console_info.dwSize.Y, cursor_zero, &wchs);
    SetConsoleCursorPosition(cons, cursor_zero);
    printf("[Joy2TX v%s] [%s: %s%s] [Int: %dms] [Act CHs: %d]     \n\n",  
            VERSION, comname, DEVNAME[dev], (COMMS) ? "" : " (DISABLED)", interval, chs );
       
    //
    // main loop
    //
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    
    while(1) {
        if(DBG) QueryPerformanceCounter(&t_start);
        SetConsoleCursorPosition(cons, cursor_home);

       // by default set all channels to on for SC8000
       if(dev==DEV_SC8000) 
           sc8000_chmask=255;
    
        //
        // set every channel
        //
        for(i=1; i<=CHANNELS; i++) {
            
            // joystick input
            if(ch[i].def==CH_JOY) {
            
                if((joyGetPosEx(ch[i].joy, &joydata)!=JOYERR_NOERROR) ||  
                    joyGetDevCaps(ch[i].joy, &joycaps, sizeof(JOYCAPS))!=JOYERR_NOERROR) {
                    fprintf(stderr, "ERROR: joystick %d unresponsive\n", ch[i].joy);
                    return 1;
                }
                
                // X axis
                if(ch[i].axis=='x') {
                    ch[i].data=DEVCHMIN[dev]+(joydata.dwXpos-joycaps.wXmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wXmax-joycaps.wXmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwXpos);
                } 
                else if(ch[i].axis=='X') {
                    ch[i].data=DEVCHMAX[dev]-(joydata.dwXpos-joycaps.wXmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wXmax-joycaps.wXmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwXpos );
                } 
    
                // Y axis
                else if(ch[i].axis=='y') {
                    ch[i].data=DEVCHMIN[dev]+(joydata.dwYpos-joycaps.wYmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wYmax-joycaps.wYmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwYpos);
                }
                else if(ch[i].axis=='Y') {
                    ch[i].data=DEVCHMAX[dev]-(joydata.dwYpos-joycaps.wYmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wYmax-joycaps.wYmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwYpos );
                } 
                
                // Z axis
                else if(ch[i].axis=='z') {
                    ch[i].data=DEVCHMIN[dev]+(joydata.dwZpos-joycaps.wZmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wZmax-joycaps.wZmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwZpos);
                }
                else if(ch[i].axis=='Z') {
                    ch[i].data=DEVCHMAX[dev]-(joydata.dwZpos-joycaps.wZmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wZmax-joycaps.wZmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwZpos );
                } 
                
                // R axis
                else if(ch[i].axis=='r') {
                    ch[i].data=DEVCHMIN[dev]+(joydata.dwRpos-joycaps.wRmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wRmax-joycaps.wRmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwRpos);
                }
                else if(ch[i].axis=='R') {
                    ch[i].data=DEVCHMAX[dev]-(joydata.dwRpos-joycaps.wRmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wRmax-joycaps.wRmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwZpos );
                } 
                
                // V axis
                else if(ch[i].axis=='v') {
                    ch[i].data=DEVCHMIN[dev]+(joydata.dwVpos-joycaps.wVmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wVmax-joycaps.wVmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwVpos);
                }
                else if(ch[i].axis=='V') {
                    ch[i].data=DEVCHMAX[dev]-(joydata.dwVpos-joycaps.wVmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wVmax-joycaps.wVmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwZpos );
                } 
                
                // U axis
                else if(ch[i].axis=='u') {
                    ch[i].data=DEVCHMIN[dev]+(joydata.dwUpos-joycaps.wUmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wUmax-joycaps.wUmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwUpos);
                }
                else if(ch[i].axis=='U') {
                    ch[i].data=DEVCHMAX[dev]-(joydata.dwUpos-joycaps.wUmin)*(DEVCHMAX[dev]-DEVCHMIN[dev])/(joycaps.wUmax-joycaps.wUmin);
                    printf("\tch[%d]=%6d <-- j%d[%c]=%6d      \n", i, ch[i].data, ch[i].joy, ch[i].axis, joydata.dwZpos );
                } 
                
                // unknown axis - shouldn't fall here
                else {
                    if(dev==DEV_PCBUDDY) {
                        ch[i].data=255;
                    }
                    else if(dev==DEV_SC8000) {
                        ch[i].data=8000;
                        sc8000_chmask&=~(1<<(i-1));
                    }
                    printf("\tch[%d]=ERROR:UNKNOWN AXIS: %c  \n", i, ch[i].axis);
                }
            } 

            // joystick button
            else if(ch[i].def==CH_BUT) {
                ch[i].data=255;
                //WARNING I OVERLAPS
                printf("\tch[%d]=%d    ", i, joydata.dwButtons);
                        for (i=0;i<16;i++) 
                            putchar((joydata.dwButtons & (1<<i)) ? '1' : '0');
                putchar('\n');

            }
            // fixed value
            else if(ch[i].def==CH_FIX) {
                ch[i].data=ch[i].fixed;
                printf("\tch[%d]=%d   \n", i, ch[i].data);
            } 

            // off or not defined            
            else if(ch[i].def==CH_OFF) {
                if(dev==DEV_PCBUDDY) {
                    ch[i].data=255;
                }
                else if(dev==DEV_SC8000) {
                    ch[i].data=8000; 
                    sc8000_chmask&=~(1<<(i-1));
                }
                printf("\tch[%d]=off   \n", i);
            } 
            
            // unknown channel definition - shouldn't fall here
            else {
                if(dev==DEV_PCBUDDY) {
                    ch[i].data=255;
                }
                else if(dev==DEV_SC8000) {
                    ch[i].data=8000; 
                    sc8000_chmask&=~(1<<(i-1));
                }
                printf("\tch[%d]=ERROR:UNKNOWN CHANNEL TYPE %d  \n", i, ch[i].def);
            }
        
        } // for i in channels

        //        
        // emit channel data to device 
        //
        emitlen=0;
        if(dev==DEV_PCBUDDY) {
            for(i=1; i<=CHANNELS; i++)
                buff[emitlen++]=ch[i].data;
        }
        else if(dev==DEV_SC8000) {
            buff[emitlen++]='~';
            buff[emitlen++]='~';
            buff[emitlen++]=sc8000_chmask;
            for(i=1; i<=CHANNELS; i++) {
                if(ch[i].def) {
                    p=(unsigned char *) &ch[i].data;
                    buff[emitlen++]=p[HI];
                    buff[emitlen++]=p[LO];
                }
            }
        }

        if(COMMS) {
            WriteFile(comhandle, buff, emitlen, &wchs, NULL);
            FlushFileBuffers(comhandle);
            
            if(wchs!=emitlen) {
                errcnt++;
                _snprintf(errmsg, 128, "Joy2TX: COMM ERRORS! [%d]   ", errcnt);      
                SetConsoleTitle(errmsg);                
            }      
        }
        
        if(DBG) {
            printf("\n\n");
            if(dev==DEV_SC8000) {
                printf(" SC8000_CHMASK=%d [", sc8000_chmask);
                for (i=0;i<8;i++) 
                    putchar((sc8000_chmask & (1<<i)) ? '1' : '0');
                putchar(']');
            }
            QueryPerformanceCounter(&t_end);
            printf(" emitlen=%d, wchs=%d, errcnt=%d, COMMS=%d \n--- %I64d ---   \n", 
                emitlen, wchs, errcnt, COMMS, t_end.QuadPart-t_start.QuadPart);
        }
        
        Sleep(interval);
    } // while(1) 
    
    return 0;
}
