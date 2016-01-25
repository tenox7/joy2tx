//
// Joy2TX, Version 1.0
// --------------------------------------------------
// PC Joystick -> PCBUDDY Interface -> RC-TX Unit
// Sample/Demo Application for  Linux 2.4 Kernels
// --------------------------------------------------
// Copyright (c) 2004 by Antoni Sawicki <as@tenox.tc>
// Licensed under GPL, terms and conditions apply
// PCBUDDY Interface is a product of RC-ELECTRONICS
// The original concept by Ken Hewitt
// --------------------------------------------------
// Compilation (gcc-x86-linux):
//      gcc joy2tx_pcbuddy_linux.c -o joy2tx -lcurses
//

// PCBUDDY operates internally on 22ms frame rate
// this needs to be set little bit higher
#define INTERVAL 30 

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sched.h>
#include <curses.h>
#include <termios.h>
#include <linux/joystick.h>


#define VER "Joy2TX_PCBUDDY: Version 1.0; Copyright (c) 2004 by Antoni Sawicki <as@tenox.tc>"

int main(int argc, char **argv) {

    // scheduler data
    struct sched_param prio;

    // joystick stuff
    int joy, joy_x=0, joy_y=0;
    char joyname_buff[1024], joyname[50];
    struct js_event jevent;

    // comm & tx stuff
    struct termios cparms;
    int comm, status;
    unsigned char ch[10];


    //
    // app init
    //
    if(argc!=3) {
        fprintf(stderr, "%s\nUsage: %s <joystick_device> <comm_device>\nExample: %s /dev/input/js0 /dev/ttyS0\n",
            VER, argv[0], argv[0]);
        exit(1);
    }

    // set realtime priority
    prio.sched_priority=sched_get_priority_max(SCHED_FIFO);
    sched_setscheduler(0, SCHED_FIFO, &prio);


    //
    // joystick init
    //
    joy = open(argv[1], O_RDONLY | O_NONBLOCK);
    if(joy<1) {
        fprintf(stderr, "Unable to open %s as joystick.\n", argv[1]);
        exit(1);
    }

    if(ioctl(joy, JSIOCGNAME(sizeof(joyname_buff)), joyname_buff) < 0)
        snprintf(joyname, sizeof(joyname), "Unknown");
    else 
        snprintf(joyname, sizeof(joyname), "%s", joyname_buff);


    //
    // comm & pcbuddy init
    //
    comm = open(argv[2], O_RDWR | O_NOCTTY | O_NDELAY);
    if(comm<1) {
        fprintf(stderr, "Unable to open %s as comm port.\n", argv[2]);
        exit(1);
    }

    if(tcgetattr(comm, &cparms) != 0) {
        fprintf(stderr, "Unable to initialize %s\n", argv[2]);
        exit(1);
    }

    cfmakeraw(&cparms);
    cfsetospeed(&cparms, B19200);
    cparms.c_cflag |= CRTSCTS;

    if(tcsetattr(comm, TCSADRAIN, &cparms) != 0) {
        fprintf(stderr, "Unable to initialize %s\n", argv[2]);
        exit(1);
    }

    // reset pcbuddy (interface is powered by RTS signal)
    ioctl(comm, TIOCMGET, &status);
    status &= ~TIOCM_RTS;
    ioctl(comm, TIOCMSET, &status);
    sleep(1);
    status |= TIOCM_RTS;
    ioctl(comm, TIOCMSET, &status);

    // unused; for buffer safety & and ch index starting with 1
    ch[0]=0;
    ch[9]=0;


    //
    // screen init
    //
    initscr();
    erase();
    mvprintw(0,0, "================================================================================");
    mvprintw(1,0, "%s", VER);
    mvprintw(2,0, "[Input: %s (%s)]", argv[1], joyname);
    mvprintw(3,0, "[Output: %s (PCBUDDY) 19200 8N1 RTS+] [Interval: %dms]", argv[2], INTERVAL);
    mvprintw(4,0, "================================================================================");
    mvprintw(6,0, "     \tInput\tOutput");
    mvprintw(7,0, "   -------------------");

    //
    // main loop
    //
    while(1) {
        if(read(joy, &jevent, sizeof(struct js_event)) == sizeof(struct js_event)) {
            //
            // process event queue
            //
            switch (jevent.type) {
                case JS_EVENT_AXIS: // joystick moved
                    switch (jevent.number) {
                        case 0: joy_x=jevent.value; break;
                        case 1: joy_y=jevent.value; break;
                    }
                    break;
                // add more event types (buttons, pedals, etc.) here
            }
        } else {
            //
            // process and send data
            //

            // convert joystick to tx-unit channel data
            // (pcbuddy range is 0-250, 255 is off)
            ch[1]=(joy_x+32767)/262; 
            ch[2]=(joy_y+32767)/262; 
            ch[3]=255; 
            ch[4]=255; 
            ch[5]=255; 
            ch[6]=255; 
            ch[7]=255; 
            ch[8]=255; 

            // send out to pcbuddy
            tcflush(comm, TCIOFLUSH);
            write(comm, ch+1, 8);


            // print out to screen
            mvprintw(8,0, "                            \r   X:\t%d\t%d", joy_x, ch[1]);
            mvprintw(9,0, "                            \r   Y:\t%d\t%d", joy_y, ch[2]);
            move(0,0);
            refresh();

            // return some cpu time to system
            usleep(INTERVAL * 1000);
        }
    }

    endwin();
    close(comm);
    close(joy);
    return 0;
}
