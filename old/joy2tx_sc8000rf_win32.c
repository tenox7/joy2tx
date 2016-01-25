//
// Joy2TX, Version 1.1
// --------------------------------------------------
// PC Joystick -> SC-8000RF Interface -> RC-TX Unit
// Sample/Demo Application for Windows NT/2K/XP/NET
// --------------------------------------------------
// Copyright (c) 2005 by Antoni Sawicki <as@tenox.tc>
// Licensed under GPL. Terms and conditions apply
// SC-8000RF Interface is a product of Tom's RC
// This version has been sponsored by TCNI
// --------------------------------------------------
//

#include <windows.h>
#include <stdio.h>
#include <mmsystem.h>

#pragma comment (lib, "winmm.lib")

#define COMM "COM8"
#define JOYSTICK 0
#define INTERVAL 30  

#define VER "Joy2TX: Version 1.1, Copyright (c) 2005 by Antoni Sawicki <as@tenox.tc>"

#define HI 1
#define LO 0

int main(void) {

    //
    // init stuff
    //

    HANDLE con;
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    COORD cursor_home = {0,6}, cursor_zero = {0,0};

    HANDLE comm;
    DCB dcb;
    JOYINFOEX joy;

    DWORD wchars;
    char string[1024];
    char commbuff[32];

    // joystick axes
    int x, y, z, r;
    unsigned char *px, *py, *pz, *pr;

    joy.dwSize=sizeof(JOYINFOEX);
    joy.dwFlags=JOY_RETURNX|JOY_RETURNY|JOY_RETURNZ|JOY_RETURNV|JOY_RETURNU|JOY_RETURNR;


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

    printf("%s\n[Interface: SC8000RF] [IN: JOY%d] [OUT: %s] [FR: %dms] [PRI: RealTime]\n", VER, JOYSTICK, COMM, INTERVAL);

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

    // set new settings
    if(!SetCommState(comm, &dcb)) {
        fprintf(stderr, "I/O Error: Unable to set communication parameters for %s:.\n", COMM);
        return 1;
    }

    // remove break signal (if any set)
    EscapeCommFunction(comm, CLRBREAK);


    printf("====[ STATUS ]=========================================================\n\n        Joystick        Channel\n ---------------------------------");

    //
    // main loop
    //

    while(joyGetPosEx(JOYSTICK, &joy)==JOYERR_NOERROR) {

        // conversion table
        x=(joy.dwXpos/4.681)+8000;
        y=(joy.dwYpos/4.681)+8000;
        z=(joy.dwZpos/4.681)+8000;
        r=(joy.dwRpos/4.681)+8000;


        // Console window update
        _snprintf(string, 1024, 
            " X: %12d  ->  4: %6d   \n"
            " Y: %12d  ->  2: %6d   \n"
            " Z: %12d  ->  1: %6d   \n"
            " R: %12d  ->  3: %6d   \n"
            "\n\n",
            joy.dwXpos, x,
            joy.dwYpos, y,
            joy.dwZpos, z,
            joy.dwRpos, r
        );

        SetConsoleCursorPosition(con, cursor_home);
        WriteConsole(con, string, strlen(string), &wchars, NULL);

        // Send out the data
        px=(unsigned char *) &x;
        py=(unsigned char *) &y;
        pz=(unsigned char *) &z;
        pr=(unsigned char *) &r;

        _snprintf(commbuff, 32, "~~\0170%c%c%c%c%c%c%c%c", 
            pz[HI], pz[LO], // ch1
            py[HI], py[LO], // ch2
            pr[HI], pr[LO], // ch3
            px[HI], px[LO]  // ch4
        );

        WriteFile(comm, commbuff, strlen(commbuff), &wchars, NULL);

        // flush output to comm port & sleep
        FlushFileBuffers(comm);

        Sleep(INTERVAL);
    }

    return 0;
}
