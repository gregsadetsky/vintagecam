CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags libusb-1.0 libturbojpeg)
LDLIBS += $(shell pkg-config --libs libusb-1.0 libturbojpeg)

vintagecam: main.c export.c zc3xx.c stv06xx.c xirlink-cit.c cam.h gspca/jpeg.h gspca/zc3xx-reg.h gspca/zc3xx-gc0303.h gspca/zc3xx-pas106b.h
	$(CC) $(CFLAGS) -o $@ main.c export.c zc3xx.c stv06xx.c xirlink-cit.c $(LDLIBS) -lpthread

# live preview only
view: vintagecam
	./vintagecam --preview $(ARGS)

# record every frame to recordings/ with a preview window
record: vintagecam
	./vintagecam --record recordings --preview $(ARGS)

clean:
	rm -f vintagecam

.PHONY: view record clean
