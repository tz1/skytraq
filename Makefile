all: skytraq
	ln -sf skytraq setagps
	ln -sf skytraq getagps

skytraq: skytraq.c

