Amiga floppydisk reading vi BeagleBone PRU.

This program has several different ways to read out the timing data.
Most of these subcomands, are in a buggy state or completly not working.

# 2025.09.06 Updates

I've been working on the programs `read_flux` and `write_flux` lately.
`write_flux` will write the data of an `.ipf` file to disk,
and `read_flux` will currently only verify that the data on the disk
matches the given `.ipf` file.

Reading and writing with these programs seems to work, when I just test
them with `bb_floppy`, but I can not get the disks I've written
to work on my Amiga computers.

I now tested `read_timing` and `write_timing` which live inside
`src/read_track_timing.c`.
There were lots of comments in those programs telling me to not
use them. I tested reading a disks content with `read_timing`,
and writing the data back to a spare with `write_timing`, and
saw that the result was an actual working clone, I could boot
on my Amiga.

I now have to investigate the difference between disks written with
`write_flux` vs disks written from `write_timing`, as both report
ok when testing with `read_flux`.

# Probably outdated info below here.

The functions called pru_read_timing and pru_write_timing, which are
the last functions to be added to this code seems to be the only ones
that are almost working.

## TO TEST:

   $ echo "cape-bb-floppy" > $SLOTS
   $ ./bb_floppy reset



