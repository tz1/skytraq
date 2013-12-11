skytraq
=======

Code to setup the Venus series of chips.

"skytraq" will send the commands to turn the various features on and off, WAAS, AGPS, and the rest.

It also implements downloading (via wget) the AGPS data from the skytraq server and uploading it to the chip

(via the links getagps and setagps <device>).

The device name defaults to /dev/ttyAMA0 for the raspberry pi.

This is an early version.

Included is a stripped version of my webgpsd NMEA decoder (so that hot/warm/cold start can use the current location or a "last location").
