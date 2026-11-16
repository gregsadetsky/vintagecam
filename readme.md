# vintagecam

userspace drivers for old (pre-uvc) usb webcams on macos. the linux kernel
still knows how to talk to these cameras, macos never did, so this ports the
kernel's gspca drivers to libusb and adds a recorder that tries hard not to
lose or corrupt frames.

tested on macos 15, apple silicon, with the real cameras listed below.

```sh
brew install libusb ffmpeg jpeg-turbo
make
./vintagecam --list         # supported cameras that are plugged in, and their usb path
make view                   # preview window for the first camera
make record                 # record it to recordings/, with a preview window
make wall                   # record every camera, one window showing all of them
```

## cameras

| camera | usb id | chips | modes (measured) |
| --- | --- | --- | --- |
| creative vf0250 | 041e:4051 | zc3xx + gc0303 | 640x480 ~14 fps, 320x240 ~28 fps, jpeg |
| creative webcam nx (pd1110) | 041e:401c | zc3xx + pas106 | 352x288, ~9 to 37 fps depending on the scene, jpeg. 176x144 written, never run |
| logitech quickcam express | 046d:0840 | stv0600 + pb0100 | 320x240 ~10 fps, 352x288 ~8 fps, raw bayer |
| lego camera / quickcam web | 046d:0850 | stv0610 + vv6410 | 356x292 ~8 fps, raw bayer |
| ibm pc camera (xirlink c-it) | 0545:8080, version 3.0a | xirlink cit model 2 | 320x240 ~9 / 6.5 / 3.5 fps by exposure step, 352x288 ~4.6 fps, raw bayer |
| sony eyetoy | 054c:0154 | ov519 + ov7648 | 640x480 ~7 fps, 320x240 ~15 fps, jpeg |

the first size is the default, `--width N` picks another. only these exact
chip pairs are ported: a quickcam express with an hdcs sensor, other ibm
models, other ov519 sensors and so on are refused at start with a message.

## how it runs

one process drives one camera. `--device USBPATH` (from `--list`) picks which.

- `--record DIR` writes every good frame to
  `DIR/<time>-<camera>-usb<path>.mjpeg`: the camera's own jpeg bytes if it
  sends jpeg, otherwise the raw bayer frame demosaiced and encoded at quality
  95. next to it: `.csv` (frame number, seconds since the first frame, unix
  time, byte offset, size, camera frame counter), `.events.csv` (faults and
  recoveries) and `.bad` (up to 200 rejected frames). existing files are never
  overwritten.
- `--preview` opens an ffplay window fed only the latest frame, so it can lag
  or be closed without touching the recording.
- `--stdout` writes the frame stream to a pipe.

`make wall` (or `./wall.sh DIR`) starts one recorder per connected camera and
one grid window, `vintagegrid`, that shows the latest frame of every recording
growing in the folder. the grid only reads the files, so it cannot disturb a
recorder. a tile goes dim when its camera has sent nothing for 2s. closing the
grid stops all recorders. a recorder that dies is started again, up to 5 times.

`--export` turns a recording into constant frame rate video in real time. a
skipped frame repeats the previous one, a gap over 0.5s becomes black:

```sh
./vintagecam --export rec.mjpeg --fps 30 | ffmpeg -f mjpeg -framerate 30 -i - -c copy rec.mkv
```

## when things go wrong

every jpeg frame is fully decoded before it is written, and one that does not
decode cleanly at the expected size is dropped. raw frames are checked for
their exact byte count.

a watchdog restarts things when there has been no good frame for 2s, 20 bad
frames in a row, a usb error, or the camera vanished: first the stream, then
the device, then a usb port reset, then it waits for the camera for as long as
it takes. the recording file stays open through all of it, so an outage is a
gap in the index and not a broken file.

when a camera comes back on another usb port its recorder follows it, as long
as it is the only free camera of that model. a different model sitting on the
old port is left alone. identical cameras have no serial numbers, so with two
free ones of the same model the recorder waits on its original port.

what it cannot catch: a picture that is valid but looks wrong.

## flags

`--width N`, `--exposure N` / `--gain N` (fixed values where the driver has
its own auto exposure: quickcam, lego, ibm), `--freq 0|50|60` (mains flicker,
zc3xx and eyetoy), `--bayer grbg|rggb|bggr|gbrg`, `--alt N` (usb bandwidth
reservation), `--frames N`, `--seconds S`, `-v`, `--dump-packets N`,
`--reg HEX=HEX` / `--i2c HEX=HEX` (override a bridge / sensor register value
the driver writes, for experiments),
`--test-faults SEC:stall|corrupt|reset|break|abandon|hide,...` (make things go
wrong on purpose at those seconds).

## adding a camera

the engine is shaped like linux gspca (`init`, `start`, `poll`, `stop`,
`pkt_scan`, `frame_add` with FIRST/INTER/LAST/DISCARD), so a gspca subdriver
ports over mostly by copying. write a `struct cam_driver`, list it in
`drivers[]` in main.c. a driver that sets `.bayer` hands over raw frames and
the engine makes the jpegs. `--dump-packets` shows raw iso packets for checking
frame headers against the kernel source.

## things the kernel drivers did not cover

found on the real cameras. each is a small deviation from the kernel code,
commented where it lives. "why" is unknown for all of them.

- vf0250 at 320x240: with the kernel table as is, green comes out swapped with
  red and blue. sensor register 0x17 = 0xe9 instead of 0xe8 fixes it (checked
  on colour bars). `--i2c 17=e8` brings the old behaviour back.
- quickcam express: the sensor's own auto exposure sat near white for every
  target tried, so the driver runs its own from the frame brightness. the lego
  camera reuses it.
- quickcam and lego: a recorder that died without stopping the stream left the
  bridge unable to start again. the driver switches the stream off first.
- lego: one bridge register write is answered with a stall. the kernel ignores
  the error, so does this.
- ibm: the start of frame marker is just `00 ff`, which can also be two
  pixels. a marker that would end a frame early is taken as picture. exposure
  is one three step setting that also sets the frame rate, the driver picks
  the step from the picture.
- eyetoy: each jpeg is padded with zero bytes after its end marker, which are
  trimmed. the camera flags about 1 frame in 100 as bad itself, more with many
  cameras running, those are skipped.
- webcam nx: after a replug one unit kept identifying itself as ff1e:ffff
  while working normally. that id is accepted too, the sensor probe still has
  to pass.

other notes:

- macos leaves these devices unconfigured, the engine sets configuration 1.
- plugging or unplugging anything can stall cameras on other usb buses for a
  moment. they recover, but do not replug mid take.
- seven cameras (one of each, two ibms) recorded together on one laptop, four
  of them on a single full speed bus behind hubs.
- starting a recorder at the instant a camera is being reset can crash inside
  libusb's own device scan (1.0.30). `wall.sh` restarts it.
- no white balance for the raw cameras yet, no audio, no snapshot buttons, no
  virtual camera for other apps yet.

## license

GPL-2.0-or-later, see LICENSE. the drivers are ports of the linux kernel's
gspca drivers (zc3xx, stv06xx, xirlink_cit, ov519) and `gspca/` holds tables
and headers copied from the kernel. the original authors are named at the top
of each file.
