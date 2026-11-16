CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags libusb-1.0)
LDLIBS += $(shell pkg-config --libs libusb-1.0)

vintagecam: main.c zc3xx.c cam.h gspca/jpeg.h gspca/zc3xx-reg.h gspca/zc3xx-gc0303.h
	$(CC) $(CFLAGS) -o $@ main.c zc3xx.c $(LDLIBS)

# live preview
view: vintagecam
	./vintagecam $(ARGS) | ffplay -loglevel warning -f mjpeg -fflags nobuffer -flags low_delay -i -

clean:
	rm -f vintagecam

.PHONY: view clean
