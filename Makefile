all: skytraq gpsdata
	ln -sf skytraq setagps
	ln -sf skytraq getagps

skytraq: skytraq.c

gpsdata: gpsdata.c

