//
// Joy2TX, Version 1.0
// --------------------------------------------------
// PC Joystick -> PCBUDDY Interface -> RC-TX Unit
// Sample/Demo Application for Windows NT/2K/XP/NET
// --------------------------------------------------
// Copyright (c) 2003 by Antoni Sawicki <as@tenox.tc>
// Licensed under GPL. Terms and conditions apply
// PCBUDDY Interface is a product of RC-ELECTRONICS
// The original concept by Ken Hewitt
// --------------------------------------------------
// Compilation (lcc-win32):
//               lc pcbuddy_joy2tx.c -o joy2tx.exe -s
// --------------------------------------------------
//

#include <stdio.h>
#include <mmsystem.h>

#pragma lib <winmm.lib>
#pragma optimize(3)

#define COMM "COM1"
#define JOYSTICK 0
#define INTERVAL 30  // PCBUDDY operates internally on 22ms frame rate, so
                     // setting it bit higher here gives a "safety buffer"


#define VER "Joy2TX: Version 1.0, Copyright (c) 2003 by Antoni Sawicki <as@tenox.tc>"


int main(void) {

    //
    // init stuff
    //

    HANDLE con;
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    COORD cursor_home = {0,8}, cursor_zero = {0,0};

    HANDLE comm;
    DCB dcb;
    JOYINFO joy;

    DWORD wchars;
    char string[1024];
    unsigned char ch[10];


    // setting realtime priority is recommended in order for all data to be sent at same time;
    // this application takes very little CPU time and shouldn't cause any delays to the system;
    // on another hand if another application running simultaneously (for example video decoder)
    // would be taking some CPU - it would cause violent shaking to servos, etc.
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

    // console i/o
    con = GetStdHandle(STD_OUTPUT_HANDLE);

    GetConsoleScreenBufferInfo(con, &console_info);
    SetConsoleCursorPosition(con, cursor_zero);
    FillConsoleOutputCharacter(con, ' ', console_info.dwSize.X * console_info.dwSize.Y, cursor_zero, &wchars);
    SetConsoleTitle("Joy2TX");

    printf("%s\n[Interface: PCBUDDY] [IN: JOY%d] [OUT: %s] [FR: %dms] [PRI: RealTime]\n", VER, JOYSTICK, COMM, INTERVAL);


    // joystick i/o
    printf("Number of Joystick devices: %d, Using device: %d.\n", joyGetNumDevs(), JOYSTICK);


    // serial i/o
    comm=CreateFile(COMM, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

    if(comm==INVALID_HANDLE_VALUE) {
        fprintf(stderr, "I/O Error: Unable to open %s:.\n", COMM);
        return 1;
    }

    // before we do anything else - purge all the garbage
    if(!PurgeComm(comm, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR )) {
        fprintf(stderr, "I/O Error: Unable to purge %s:.\n", COMM);
        return 1;
    }

    // get current settings
    if(!GetCommState(comm, &dcb)) {
        fprintf(stderr, "I/O Error: Unable to retreive %s: state.\n", COMM);
        return 1;
    }


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

    // set new settings
    if(!SetCommState(comm, &dcb)) {
        fprintf(stderr, "I/O Error: Unable to set communication parameters for %s:.\n", COMM);
        return 1;
    }

    // remove break signal (if any set)
    EscapeCommFunction(comm, CLRBREAK);

    printf("Serial port %s: initialized [9600/8N1/NOFLOW/DTRON/RTSON].\n", COMM);

    // reset pcbuddy (it's powered by RTS controll line)
    EscapeCommFunction(comm, CLRRTS); // off
    sleep(100);
    EscapeCommFunction(comm, SETRTS); // on
    sleep(100);


    printf("====[ STATUS ]=========================================================\n\n        Joystick     Channel\n ---------------------------");

    //
    // main loop
    //

    while(joyGetPos(JOYSTICK, &joy)==JOYERR_NOERROR) {

        // Set servo positions/channel values here.
        // PCBUDDY range = 0-250, Joystick range = 0-65535.
        ch[1]=250-(joy.wXpos/262); // joystick X angle - reversed
        ch[2]=joy.wYpos/262; // joystick Y angle
        ch[3]=joy.wZpos/262; // joystick Z angle
        ch[4]=255;  // off
        ch[5]=255;  // off
        ch[6]=255;  // off
        ch[7]=255;  // off
        ch[8]=255;  // off

        // this is for buffer safety only - not sent to the interface
        ch[0]=0;
        ch[9]=0;


        // Console window update
        snprintf(string, 1024, " X: %12d  ->  1:%4d   \n Y: %12d  ->  2:%4d   \n Z: %12d  ->  3:%4d    \n\n",
                                  joy.wXpos, ch[1],       joy.wYpos, ch[2],       joy.wZpos, ch[3]);

        SetConsoleCursorPosition(con, cursor_home);
        WriteConsole(con, string, strlen(string), &wchars, NULL);


        // Send out the data
        WriteFile(comm, ch+1, 8, &wchars, NULL);

        // flush output to comm port & sleep
        FlushFileBuffers(comm);

        sleep(INTERVAL);
    }

    return 0;
}
