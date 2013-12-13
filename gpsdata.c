#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "gpstat.h"
//NMEA field data extraction helpers

struct gpsstate gpst;
struct gpssats gpsat;

static char *field[100];        // expanded to 100 for G-Rays PUBX03

static int get2(char *c) {
    int i = 0;
    if(*c)
        i = (*c++ - '0') * 10;
    if(*c)
        i += *c - '0';
    return i;
}

static int get3(char *c) {
    int i = 0;
    if(*c)
        i = (*c++ - '0') * 100;
    i += get2(c);
    return i;
}

unsigned int tenexp[] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };

static int getndp(char *d, int p) {
    int i = 0;
    while(*d && *d != '.') {
        i *= 10;
        i += (*d++ - '0');
    }
    if(!p)
        return i;
    i *= tenexp[p];
    if(*d == '.')
        d++;
    while(*d && p--)
        i += (*d++ - '0') * tenexp[p];  // p == 0 can be optimized
    return i;
}

static void gethms(int i) {
    //hms field[i]
    char *c = field[i];
    gpst.hr = get2(c);
    gpst.mn = get2(&c[2]);
    gpst.sc = get2(&c[4]);
    if(c[6] && c[6] == '.')
        gpst.scth = get3(&c[7]);
}

static void getll(int f) {
    int l, d;
    char *c;
    c = field[f++];
    l = get2(c);
    c += 2;
    d = (getndp(c, 5) + 1) / 6;
    c = field[f++];
    l *= 1000000;
    l += d;
    if(*c != 'N')
        l = -l;
    //    if (l != gpst.llat)
    //        chg = 1;
    gpst.llat = l;
    c = field[f++];
    l = get3(c);
    c += 3;
    d = (getndp(c, 5) + 1) / 6;
    c = field[f];
    l *= 1000000;
    l += d;
    if(*c != 'E')
        l = -l;
    //    if (l != gpst.llon)
    //        chg = 1;
    gpst.llon = l;
}

// expects single null terminated strings (line ends dont matter)
int getgpsinfo(char *buf) {
    char *c, *d;
    int i, fmax;
    c = buf;
    d = NULL;
    // required for pathologic cases of $GPABC...$GPXYZ...*ck
    // where $GPABC... resolves to zero
    for(;;) {                   // find last $ - start of NMEA
        c = strchr(c, '$');
        if(!c)
            break;
        d = c;
        c++;
    }
    if(!d)
        return 0;
    // ignore all but standard NMEA
    if(*d != '$')
        return 0;
    if(d[1] != 'G')
        return 0;
    if(d[2] != 'P' && d[2] != 'N' && d[2] != 'L')
        return 0;
    c = d;
    c++;
    //verify checksum
    i = 0;
    while(*c && *c != '*')
        i ^= *c++;
    if(!*c || (unsigned)(i & 0xff) != strtoul(++c, NULL, 16)) {
        printf("Bad NMEA Checksum, calc'd %02x:\n %s", i, d);
        return -1;
    }
    --c;
    //null out asterisk
    *c = 0;
    c = d;
    //Split into fields at the commas
    fmax = 0;
    c += 2;
    char satype = *c++;         // P,L,N
    for(;;) {
        field[fmax++] = c;
        c = strchr(c, ',');
        if(c == NULL)
            break;
        *c++ = 0;
    }
    //Latitude, Longitude, and other info
    if(fmax == 13 && !strcmp(field[0], "RMC")) {
        //NEED TO VERIFY FMAX FOR EACH
        if( gpst.lock < 2 )
            gpst.lock = field[2][0] == 'A';
        if( gpst.lock || (!gpst.llat && !gpst.llon)) {
            gethms(1);
            getll(3);
            gpst.gspd = getndp(field[7], 3) * 1151 / 1000;
            //convert to MPH
            gpst.gtrk = getndp(field[8], 3);
            //Date, DDMMYY
            gpst.dy = get2(field[9]);
            gpst.mo = get2(&field[9][2]);
            gpst.yr = get2(&field[9][4]);
        }
        if( !gpst.lock )
            return 1;
    }
    else if(fmax == 15 && (!strcmp(field[0], "GGA") || !strcmp(field[0], "GNS"))) {
        i = field[6][0] - '0';
        // was gpst.lock, but it would prevent GPRMC alt
        if(!i)
            return 1;
        else if(gpst.lock != i)
            gpst.lock = i;
        // Redundant: getll(2);
        // don't get this here since it won't increment the YMD
        // and create a midnight bug
        //       gethms(1);
        //7 - 2 plc Sats Used
        // 8 - HDOP
        gpst.hdop = getndp(field[8], 3);
        gpst.alt = getndp(field[9], 3);
        //9, 10 - Alt, units M
    }
    else if(fmax == 8 && !strcmp(field[0], "GLL")) {
        if( gpst.lock < 2 )
            gpst.lock = field[6][0] == 'A';
        if(gpst.lock || (!gpst.llat && !gpst.llon)) {
            getll(1);
            gethms(5);
        }
        if( !gpst.lock )
            return 1;
    }
    else if(fmax == 10 && !strcmp(field[0], "VTG")) {
        gpst.gtrk = getndp(field[1], 3);
        gpst.gspd = getndp(field[5], 3) * 1151 / 1000;
        //convert to MPH
    }
    //Satellites and status
    else if(!(fmax & 3) && fmax >= 8 && fmax <= 20 && !strcmp(field[0], "GSV")) {
        int j, tot, seq;
        //should check (fmax % 4 == 3)
        tot = getndp(field[1], 0);
        seq = getndp(field[2], 0);
        if(satype == 'P') {
            if(seq == 1)
                for(j = 0; j < 65; j++)
                    gpsat.view[j] = 0;
            gpsat.pnsats = getndp(field[3], 0);
            gpsat.psatset &= (1 << tot) - 1;
            gpsat.psatset &= ~(1 << (seq - 1));
        }
        else {
            if(seq == 1)
                for(j = 65; j < 100; j++)
                    gpsat.view[j] = 0;
            gpsat.lnsats = getndp(field[3], 0);
            gpsat.lsatset &= (1 << tot) - 1;
            gpsat.lsatset &= ~(1 << (seq - 1));
        }
        for(j = 4; j < 20 && j < fmax; j += 4) {
            i = getndp(field[j], 0);
            if(!i)
                break;
            if(i > 119)   // WAAS,EGNOS high numbering
                i -= 87;
            gpsat.view[i] = 1;
            gpsat.el[i] = getndp(field[j + 1], 0);
            gpsat.az[i] = getndp(field[j + 2], 0);
            gpsat.sn[i] = getndp(field[j + 3], 0);
        }
        int n;
        if(satype == 'P' && !gpsat.psatset) {
            gpst.pnsats = 0;
            gpst.pnused = 0;
            for(n = 0; n < 65; n++) {
                if(gpsat.view[n]) {
                    int k = gpst.pnsats++;
                    gpst.psats[k].num = n;
                    gpst.psats[k].el = gpsat.el[n];
                    gpst.psats[k].az = gpsat.az[n];
                    gpst.psats[k].sn = gpsat.sn[n];
                    if(gpsat.used[n]) {
                        gpst.pnused++;
                        gpst.psats[k].num = -n;
                    }
                    else
                        gpst.psats[k].num = n;
                }
            }
        }
        // else
        if(satype == 'L' && !gpsat.lsatset) {
            gpst.lnsats = 0;
            gpst.lnused = 0;
            for(n = 65; n < 99; n++) {
                if(gpsat.view[n]) {
                    int k = gpst.lnsats++;
                    gpst.lsats[k].num = n;
                    gpst.lsats[k].el = gpsat.el[n];
                    gpst.lsats[k].az = gpsat.az[n];
                    gpst.lsats[k].sn = gpsat.sn[n];
                    if(gpsat.used[n]) {
                        gpst.lnused++;
                        gpst.lsats[k].num = -n;
                    }
                    else
                        gpst.lsats[k].num = n;
                }
            }
        }
    }
    else if(fmax == 18 && !strcmp(field[0], "GSA")) {
        gpst.fix = getndp(field[2], 0);
        gpst.pdop = getndp(field[15], 3);
        gpst.hdop = getndp(field[16], 3);
        gpst.vdop = getndp(field[17], 3);
        int j = getndp(field[3], 0);
        if(j > 119)
            j -= 87;
        if(j && j < 65) {
            gpsat.psatset = 255;
            for(i = 0; i < 65; i++)
                gpsat.used[i] = 0;
            gpsat.pnused = 0;
            for(i = 3; i < 15; i++) {
                int k = getndp(field[i], 0);
                if(k > 119)
                    k -= 87;
                if(k) {
                    gpsat.used[k]++;
                    gpsat.pnused++;
                }
                // else break;?
            }
        }
        if(j && j > 64) {
            gpsat.lsatset = 255;
            for(i = 65; i < 100; i++)
                gpsat.used[i] = 0;
            gpsat.lnused = 0;
            for(i = 3; i < 15; i++) {
                int k = getndp(field[i], 0);
                if(k > 119)
                    k -= 87;
                if(k) {
                    gpsat.used[k]++;
                    gpsat.lnused++;
                }
                // else break;?
            }
        }
    }
    else {
        printf("?%s\n", field[0]);
        return 0;
    }
    return 2;
}

#include <fcntl.h>
int main(int argc, char *argv[]) {
    int gpsfd = open(argv[1], O_RDONLY);
    if(gpsfd < 0) return gpsfd;
    char nmeastring[4096];
    int i,j;

    gpst.llat= gpst.llon= gpst.alt = 0;

    while( gpst.llat == 0 ||  gpst.llon == 0 || gpst.alt == 0 ) {
        i = read(gpsfd, nmeastring, 1);
        if(i != 1)
            return i;
        j = 1;
        for(;;) {
            i = read(gpsfd, nmeastring+j, 1);
            if(i != 1)
                return i;
            if(nmeastring[j] < ' ')
                break;
            j++;
        }
        nmeastring[j]= 0;
        i = getgpsinfo(nmeastring);
        if( i < 2 )
            continue;
        printf("L:%d %d/%d/%d %02d:%02d:%02d.%03d\n", gpst.lock, gpst.mo, gpst.dy, gpst.yr, gpst.hr, gpst.mn, gpst.sc, gpst.scth );
        printf("LAT=%d LON=%d ALT=%d\n", gpst.llat, gpst.llon, gpst.alt);
    }
}
