# Open source SeaTalk library hardware driver for Linux GPIO pins

These files are vanilla C code so *.cpp could in theory be renamed to .c if for some reason that became necessary.

This is not officially supported in any way, shape or form by anyone. *Use this at your own risk!*

This could not have been possible without Thomas Knauf's absolutely indispensible SeaTalk reference found here: http://www.thomasknauf.de/seatalk.htm. Thank you, Thomas, for all the work it must have taken to work out the protocol details! I am in awe.

This is a library only and will not compile a executable file. Related projects:
- (seatalk)[https://github.com/jamesroscoe/seatalk.git] The base SeaTalk library.
- (seatalk-hardware-linux-gpio)[https://github.com/jamesroscoe/seatalk-hardware-linux-gpio.git] Use GPIO pins on a Linux device (eg Raspberry Pi) to read and write SeaTalk data. Use in conjunciton with this library as GPIO pins must be manipulated from within kernel space.

This has been tested on Raspberry Pi.
