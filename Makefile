all: skytraq gpsdata
	ln -sf skytraq setagps
	ln -sf skytraq getagps

skytraq: skytraq.c
	gcc $< -o $@ 

gpsdata: gpsdata.c
	gcc -Wall $< -o $@ 

