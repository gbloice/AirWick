This project uses:

	Arduino IDE updates for ATTiny cores, ide-hardware from https://github.com/jcw/ide-hardware.git
	The JeeLib library from https://github.com/jcw/jeelib
	A DHT22 sensor library DHTLib from http://playground.arduino.cc/Main/DHTLib
	An updated avr compiler from Atmel.
	
The Arduino compiler has to be updated to correctly link for the t84.  I used the compiler from Atmel Studio 6.1:

	1.  Locate the current compiler used by the arduino IDE (C:\Program Files (x86)\Arduino\hardware\tools\avr) and make a copy for backup.
	2.  Locate the compiler from the Atmel Studio (C:\Program Files (x86)\Atmel\Atmel Toolchain\AVR8 GCC\Native\3.4.2.1002\avr8-gnu-toolchain).
	3.  Copy the contents of (2) over (1).
	4.  The new compiler library has a definition which clashes with that in the JeeLabs IDE support for the tiny.  In ide-hardware/avr/cores/tiny/wiring.h comment out the definition of round at line 136.
	
Flashing command line for Bus Pirate (note the COM port may need to be adjusted):

	avrdude -p t84 -c buspirate -P COM3 -U flash:w:AirWick.cpp.hex
	
The BusPirate connections track that marked on the JeeNode Micro, i.e. MISO - MISO, MOSI - MOSI, CS - RESET, CLK - SCK