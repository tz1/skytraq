#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

static int gpsfd = -3;
static unsigned char msgbuf[65600];
static unsigned buflen;
static unsigned char respbuf[65600];
static unsigned resplen;

// Convert command bytes to a full packet with checksum and other bytes
static unsigned setupbuf(unsigned len, unsigned char *buf) {
    unsigned char *c = msgbuf;
    unsigned i;
    unsigned char cks = 0;
    *c++ = 0xa0;
    *c++ = 0xa1;
    *c++ = len >> 8;
    *c++ = len;
    //printf("CMD ");
    for(i = 0; i < len; i++) {
        cks ^= buf[i];
        *c++ = buf[i];
        //printf("%02x ", buf[i]);
    }
    //printf("\n");
    *c++ = cks;
    *c++ = 0x0d;
    *c++ = 0x0a;
    buflen = (c - msgbuf);
    return buflen;
}

// get the N responses to a command
static void getresp(unsigned nresp) {
    unsigned k;
    write(gpsfd, msgbuf, buflen);
    for(k = 0; k < nresp; k++) {
        unsigned j = 0;
        while(1) {
            respbuf[0] = 0;
            if(1 != read(gpsfd, respbuf, 1)) {
                break;
            }
            if(respbuf[0] == 0xa0) {
                break;
            }
        }
        j = 1;
        while(j < 5) {
            respbuf[j] = 0;
            if(1 != read(gpsfd, &respbuf[j], 1)) {
                break;
            }
            j++;
        }
        if(respbuf[1] != 0xa1) {
            continue;
        }
        unsigned len = (respbuf[2] << 8) + respbuf[3];
        //printf( "len=%-2d ", len );
        while(j < len + 7) {
            respbuf[j] = 0;
            if(1 != read(gpsfd, &respbuf[j], 1)) {
                return;
            }
            j++;
        }
        resplen = j;
        unsigned char cks = 0;
        for(j = 0; j < len; j++) {
            cks ^= respbuf[4 + j];
        }
        if(cks != respbuf[4 + j]) {
            printf("!! cks=%02x\n", cks);
        }
        switch(respbuf[4]) {
        case 0x80:
            printf
            ("SW Version %d: krn: %d %d.%d.%d odm: %d %d.%d.%d rev: %d %d/%d/%d ",
             respbuf[5], respbuf[6], respbuf[7], respbuf[8], respbuf[9],
             respbuf[10], respbuf[11], respbuf[12], respbuf[13], respbuf[14],
             respbuf[17], respbuf[16], respbuf[15]);
            // 1=system; kernel version x.y.z; odm x.y.z, rev; 00.YY.MM.DD
            break;
        case 0x81:
            printf("FWCRC %d: %04d ", respbuf[5], (respbuf[6] << 8) + respbuf[7]);  // 1=system; crc16
            break;
        case 0x83:
            //printf( "ACK %02x ", respbuf[5] );
            continue;
            break;
        case 0x84:
            printf("NAK %02x ", respbuf[5]);
            break;
        case 0x86:
            printf("Update Rate: %d ", respbuf[5]);
            break;
        case 0xb1:      //len=87
            printf("Ephem Sat %d \n", (respbuf[5] << 8) + respbuf[6]);
            for(j = 7; j < 7 + 28; j++) {
                printf("%02x", respbuf[j]);
            }
            printf("\n");
            for(j = 7 + 28; j < 7 + 28 + 28; j++) {
                printf("%02x", respbuf[j]);
            }
            printf("\n");
            for(j = 7 + 28 + 28; j < 7 + 28 + 28 + 28; j++) {
                printf("%02x", respbuf[j]);
            }
            break;
        case 0xae:
            printf("Datum Index %d ", (respbuf[5] << 8) + respbuf[6]);
            break;
        case 0xb2:
            printf("AGPS %s %d hours ", respbuf[7] ? "enabled" : "disabled",
                   (respbuf[6] << 8) + respbuf[5]);
            break;
        case 0xb3:
            printf("WAAS %s ", respbuf[5] ? "enabled" : "disabled");
            break;
        case 0xb4:
            printf("PosPin %s ", respbuf[5] ? "enabled" : "disabled");
            break;
        case 0xb5:
            printf("NavMode %s ", respbuf[5] ? "pedestrian" : "car");
            break;
        case 0xb6: {
            char *pps[3] = { "off", "on 3D fix", "on 1 sat" };
            printf("1PPS mode %s ", pps[respbuf[5]]);
        }
        break;
        default:
            printf("(unknown) %d: ", len);
            for(j = 0; j < len; j++) {
                printf("%02x ", respbuf[4 + j]);
            }
        }
        printf("\n");
    }
}

// read a string (agps) response - null terminated
static int readtonull(int fd) {
    int l;
    char b[4];
    for(;;) {
        l = read(fd, b, 1);
        if(l <= 0)
            continue;
        if(!*b)
            break;
        printf("%c", *b);
    }
    printf("\n");
    return 0;
}

// send Eph.dat to Venus
static unsigned char ephbuf[65536*4];
static int setagps() {
    int i;
    unsigned char *ephdata;
    long ephbytes;
    ephdata = ephbuf;
    int ofd = open("/tmp/Eph.dat", O_RDONLY);
    if(ofd < 0)
        return -2;
    ephbytes = read(ofd, ephbuf, 256 * 1024);
    if(ephbytes < 65536)
        return -3;
    // checksum
    unsigned char csuma, csumb = 0;
    for(i = 0; i < 0x10000; i++)
        csumb += ephdata[i];
    csuma = csumb;
    for(; i < ephbytes; i++)
        csuma += ephdata[i];
    //set AGPS
    setupbuf(1, "\x35");
    getresp(2); // should check success
    usleep(500000);
    /* start the transmission */
    char string[128];
    sprintf(string, "BINSIZE = %ld Checksum = %d Checksumb = %d ", ephbytes, csuma, csumb);
    write(gpsfd, string, strlen(string) + 1);
    printf("%s\n", string);
    readtonull(gpsfd);
#define BLKSIZ 8192
    unsigned tot = ephbytes;
    while(ephbytes > 0) {
        printf("%ld%% ", (tot - ephbytes) * 100 / tot);
        write(gpsfd, ephdata, ephbytes > BLKSIZ ? BLKSIZ : ephbytes);
        readtonull(gpsfd);        // OK or Error, null terminated
        ephbytes -= BLKSIZ;
        ephdata += BLKSIZ;
    }
    // Status "END" or "Error2"
    readtonull(gpsfd);            // END
    // maybe get ack?
    return 0;
}

// commandlist (most are simple send strings
struct cmds {
    char *cmd;
    unsigned sendlen;
    unsigned char *send;
    char *help;
};

struct cmds cmdlist[] = {

    {"-waas", 3, "\x37\x00\x00", "disable WAAS" },
    {"+waas", 3, "\x37\x01\x00", "enable WAAS" },
    {"-waas*", 3, "\x37\x00\x01", "disable WAAS, save to flash" },
    {"+waas*", 3, "\x37\x01\x01", "enable WAAS, save to flash" },

    {"-agps", 3, "\x33\x00\x00", "disable AGPS" },
    {"+agps", 3, "\x33\x01\x00", "enable AGPS" },
    {"-agps*", 3, "\x33\x00\x01", "disable AGPS, save to flash" },
    {"+agps*", 3, "\x33\x01\x01", "enable AGPS, save to flash" },

    {"-psav", 3, "\x0c\x00\x00", "disable PowerSave (fullsearch)" },
    {"+psav", 3, "\x0c\x01\x00", "enable PowerSave (halfsearch)" },
    {"-psav*", 3, "\x0c\x00\x01", "disable PowerSave (fullsearch), save to flash" },
    {"+psav*", 3, "\x0c\x01\x01", "enable PowerSave (halfsearch), save to flash" },
    {"-psav~", 3, "\x0c\x00\x02", "disable PowerSave (fullsearch), temporary" },
    {"+psav~", 3, "\x0c\x01\x02", "enable PowerSave (halfsearch), temporary" },

    {"rate1", 3, "\x0e\x01\x00", "Set Rate to 1Hz" },
    {"rate1*", 3, "\x0e\x01\x01", "Set Rate to 1Hz, save to flash" },
    {"rate2", 3, "\x0e\x02\x00", "Set Rate to 2Hz" },
    {"rate2*", 3, "\x0e\x02\x01", "Set Rate to 2Hz, save to flash" },
    {"rate4", 3, "\x0e\x04\x00", "Set Rate to 4Hz" },
    {"rate4*", 3, "\x0e\x04\x01", "Set Rate to 4Hz, save to flash" },
    {"rate5", 3, "\x0e\x05\x00", "Set Rate to 5Hz" },
    {"rate5*", 3, "\x0e\x05\x01", "Set Rate to 5Hz, save to flash" },
    {"rate10", 3, "\x0e\x0a\x00", "Set Rate to 10Hz" },
    {"rate10*", 3, "\x0e\x0a\x01", "Set Rate to 10Hz, save to flash" },
    {"rate20", 3, "\x0e\x14\x00", "Set Rate to 20Hz" },
    {"rate20*", 3, "\x0e\x14\x01", "Set Rate to 20Hz, save to flash" },

// 25 40 50

    {"-peds", 3, "\x3c\x00\x00", "disable Pedestrian (car mode)" },
    {"+peds", 3, "\x3c\x01\x00", "enable Pedestrian" },
    {"-peds*", 3, "\x3c\x00\x01", "disable Pedestrian (car mode), save to flash" },
    {"+peds*", 3, "\x3c\x01\x01", "enable Pedestrian, save to flash" },


    {"-pps", 3, "\x3e\x00\x00", "disable Pulse Per Second" },
    {"+pps", 3, "\x3e\x01\x00", "enable  Pulse Per Second, 3D fix" },
    {"+pps1s", 3, "\x3e\x02\x00", "enable Pulse Per Second, 1 Sat locked" },
    {"-pps*", 3, "\x3e\x00\x01", "disable PowerSave (fullsearch), save to flash" },
    {"+pps*", 3, "\x3e\x01\x01", "enable PowerSave (halfsearch), save to flash" },
    {"+pps1s*", 3, "\x3e\x02\x01", "enable Pulse Per Second, 1 Sat locked, save to flash" },

    {"+FACTORY", 2, "\x04\x01", "Reset to Factory settings" },

    {"-pin", 2, "\x39\x00", "disable Position Pinning" },
    {"+pin", 2, "\x39\x01", "enable Position Pinning" },

    {"-nmea", 2, "\x09\x00", "disable position messages" },
    {"+nmea", 2, "\x09\x01", "enable NMEA messages" },
    //{"+binary", 2, "\x09\x01", "enable Binary messages" },

    {"-h", 0, "", "This help text" },
    {"--help", 0, "", "This help text" },
    {"-?", 0, "", "This help text" },

    {"-d", 0, "", "-d <device> specifies the serial device" },

    {"?swvers", 2, "\x02\x01", "Query software version" }, 
    {"?swcrc", 2, "\x03\x01", "Query software CRC16" }, 
    {"?datum", 1, "\x2d", "Query Datum" }, 

//  setupbuf (2, "\x30\x00"); // 0 - get all
//  getresp (34);

    {NULL,0,"",""},
};


static char  gpsdev[1024] = "/dev/ttyAMA0";
static int opengps() {
    gpsfd = open(gpsdev, O_RDWR);
    if(gpsfd < 0)
        return gpsfd;
    struct termios tio;
    if((tcgetattr(gpsfd, &tio)) == -1)
        return -1;
    cfmakeraw(&tio);
    if((tcsetattr(gpsfd, TCSAFLUSH, &tio)) == -1)
        return -1;
    return gpsfd;
}

int main(int argc, char *argv[]) {
    //    char gpsdev[64] = "/dev/rfcomm0";
    unsigned i,j,k;
    unsigned char msg[65536], *m;
    time_t gmtsecs = time(NULL);
    struct tm *gmt = gmtime(&gmtsecs);

    if(strstr(argv[0], "getagps") )
        return system("wget ftp://skytraq:skytraq@60.250.205.31/ephemeris/Eph.dat -O /tmp/Eph.dat");
    //return system("curl ftp://skytraq:skytraq@60.250.205.31/ephemeris/Eph.dat -o /tmp/Eph.dat");



    if(strstr(argv[0], "setagps") ) {
        if(argc > 1)
            strcpy(gpsdev, argv[1]);
        if( opengps() < 0 )
            return -1;
        return setagps();
    }


    for( i = 1; i < argc; i++ ) {
        j = 0;

        while( cmdlist[j].cmd ) {
            if( !strcmp( cmdlist[j].cmd, argv[i] ) )
                break;
            j++;
        }
        if( !cmdlist[j].cmd )
            continue;
        if( !cmdlist[j].sendlen ) {
            if( !strcmp( argv[i], "-d" ) ) {
                strncpy( gpsdev, argv[++i], sizeof(gpsdev) );
                continue;
            }
            k = 0;
            while( cmdlist[k].cmd ) {
                printf( "%-10s %s\n", cmdlist[k].cmd, cmdlist[k].help );
                k++;
            }
        }
        else {
            if( gpsfd < 0 )
                opengps();
            if( cmdlist[j].sendlen ) {
		printf( "%s\n", cmdlist[j].cmd );
                setupbuf( cmdlist[j].sendlen, cmdlist[j].send );
                getresp(cmdlist[j].cmd[0] == '?' ? 3 : 2);
            }
        }
    }

            if( gpsfd < 0 )
                opengps();

// System Restart
    m = msg;
    *m++ = 1;
    *m++ = 1;           // 1=hot, 2=warm, 3=cold
    *m++ = (gmt->tm_year + 1900) >> 8;
    *m++ = (gmt->tm_year + 1900) & 0xff;;
    *m++ = gmt->tm_mon + 1;
    *m++ = gmt->tm_mday;
    *m++ = gmt->tm_hour;
    *m++ = gmt->tm_min;
    *m++ = gmt->tm_sec;
    *m++ = 4200 >> 8;           //lat U16 deg*100
    *m++ = 4200 & 255;           //lat U16
    *m++ = -10200 >> 8;           //lon U16
    *m++ = -10200 & 255;
    *m++ = 1000 >> 8;           //alt U16 meter
    *m++ = 1000 & 255;
    setupbuf((unsigned)(m - msg), msg);
//getresp(2);

//Query Ephem
//  setupbuf (2, "\x30\x00"); // 0 - get all
//  getresp (34);
//  setupbuf (2, "\x30\x01"); // 1-32
//  getresp (3);
//  setupbuf (2, "\x30\x20"); // 1-32
//  getresp (3);
//  change 0xb1 to 0x31 as a command to upload (set) Ephem


//Set serial port - port 0, 115200, sticky (flash and sram)
// should scan bauds sending this with no response required
//bauds[] = { 4800, 9600, 19200, 38400, 57600, 115200 };, maybe 230400?
//setupbuf (2, "\x05\x00\x05\x01");
//getresp (2);


//Query Update Rate
    setupbuf(1, "\x10");
    getresp(3);
//Query WAAS Status
    setupbuf(1, "\x38");
    getresp(3);
//Query Position Pinning
    setupbuf(1, "\x3a");
    getresp(3);
//Query Navigation Mode
    setupbuf(1, "\x3d");
    getresp(3);
//Query 1PPS mode
    setupbuf(1, "\x3f");
    getresp(3);
//Query AGPS assist
    m = msg;
    *m++ = 0x34;
    *m++ = (gmt->tm_year + 1900) >> 8;
    *m++ = (gmt->tm_year + 1900) & 0xff;;
    *m++ = gmt->tm_mon + 1;
    *m++ = gmt->tm_mday;
    *m++ = gmt->tm_hour;
    *m++ = gmt->tm_min;
    *m++ = gmt->tm_sec;
    setupbuf((unsigned)(m - msg), msg);
    getresp(3);






// (pinning params)
    m = msg;
    *m++ = 0x3b;
unsigned short PinSpeedKPH=2, CntSeconds=3, UnPinSpeedKPH=5, UnPinCntSecs=3, UnPinDistMeters=5; 
    *m++ = PinSpeedKPH>>8;
    *m++ = PinSpeedKPH&0xff;
    *m++ = CntSeconds>>8;
    *m++ = CntSeconds&0xff;
    *m++ = UnPinSpeedKPH>>8;
    *m++ = UnPinSpeedKPH&0xff;
    *m++ = UnPinCntSecs>>8;
    *m++ = UnPinCntSecs&0xff;
    *m++ = UnPinDistMeters>>8;
    *m++ = UnPinDistMeters&0xff;
    setupbuf((unsigned)(m - msg), msg);
//getresp(2);

//Set NMEA rates "every second", maybe every rate interval
    m = msg;
    *m++ = 0x08;
    *m++ = 1; // GGA
    *m++ = 1; // GSA
    *m++ = 1; // GSV
    *m++ = 0; // GLL
    *m++ = 1; // RMC
    *m++ = 0; // VTG
    *m++ = 0; // ZDA
    *m++ = 1;//toflash;
    setupbuf((unsigned)(m - msg), msg);
//getresp(2);

//Binary Message rate
    m = msg;
    *m++ = 0x12;
    *m++ = 5; // rate - [1,2,4,5,10,20]...more = 20
    *m++ = 1; // MeasTime
    *m++ = 1; // RawMeas
    *m++ = 1; // SVCHStatus
    *m++ = 0; // RCVstate
    *m++ = 0; // Subframe
    *m++ = 1; //toflash;
    setupbuf((unsigned)(m - msg), msg);
//getresp(2);



close(gpsfd);
return 0;


}
