// Copyright 2008, Tom Zerucha, tz@execpc.com, 
// released under GNU General Public License version 3.

// data billboard
struct satinfo {
  // satellite number, elevation, azmuith, and signal
  // satellite number is NEGATIVE if used
    short num, el, az, sn;
};

struct gpsstate {
  // latitude, longitude in micro-degrees.  Altitude in feet * 1000
    int llat, llon, alt;
  // dilution of precision * 1000
    int pdop, hdop, vdop;
  // speed, mph * 1000, track, degrees * 1000
    int gspd, gtrk;
  // year (in century, 08 not 2008), month, day, hour, minute, second, thousanths
    int yr, mo, dy, hr, mn, sc, scth;
  // lock, 0-1.  fix from GPGSA
    int lock, fix;
  // number of sats visible, number being used in fix
    int pnsats, pnused;
    int lnsats, lnused;
  // satellite table
    struct satinfo psats[32];
    struct satinfo lsats[32];
};

struct gpssats {
    int psatset;
    int lsatset;
    int pnsats, pnused;
    int lnsats, lnused;
    int used[100], view[100]; // list of used, inview
    int el[100], az[100], sn[100];
};

extern struct gpsstate gpst;
extern struct gpssats gpsat;

