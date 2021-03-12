# Open source SeaTalk library hardware driver for Linux GPIO pins

This is not officially supported in any way, shape or form by anyone. *Use this at your own risk!*

This could not have been possible without Thomas Knauf's absolutely indispensible SeaTalk reference found here: http://www.thomasknauf.de/seatalk.htm. Thank you, Thomas, for all the work it must have taken to work out the protocol details! I am in awe.

This is a library only and will not compile a executable file. Related projects:
- (seatalk)[https://github.com/jamesroscoe/seatalk.git] The base SeaTalk library.
- (seatalk-instruments)[https://github.com/jamesroscoe/seatalk-instruments.git] Provides an API for managing boat status and pushing locally-generated sensor data and commands onto the Seatalk bus. This is usd in the Linux kernel extension project below but could theoretically be incorporated into an application.
- (seatalk-linux-kext)[https://github.com/jamesroscoe/seatalk-linux-kext.git] Use GPIO pins on a Linux device (eg Raspberry Pi) to read and write SeaTalk data. Use in conjunciton with this library as GPIO pins must be manipulated from within kernel space.

This has been tested on Raspberry Pi.
