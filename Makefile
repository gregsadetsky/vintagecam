CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags libusb-1.0)
LDLIBS += $(shell pkg-config --libs libusb-1.0)

vintagecam: main.c zc3xx.c cam.h gspca/jpeg.h gspca/zc3xx-reg.h gspca/zc3xx-gc0303.h
	$(CC) $(CFLAGS) -o $@ main.c zc3xx.c $(LDLIBS) -lpthread

# live preview only
view: vintagecam
	./vintagecam --preview $(ARGS)

# record every frame to recordings/ with a preview window
record: vintagecam
	./vintagecam --record recordings --preview $(ARGS)

clean:
	rm -f vintagecam

.PHONY: view record clean
