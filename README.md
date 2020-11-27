# IRUSB
This project is is a small board for sending infrared controller codes from a PC.
Receiving IR codes is possible too, but was not the main objective of this project.

![alt text](pictures/outside-small.jpg "PCB in case and empty PCB")

![alt text](pictures/inside-small.jpg "PCB soldered together with debug wires")

## Host program

A host program for communicating is provided.
Written in python, it should be platform independend. But only Linux was tested.

It all goes down to call the program like

./irusbControl.py 2 0xff00 0

Where the first parameter is the protocol, the second the address and the third the command.

You can add a TSOP31236 and call the host program without a parameter. Then press the key, you want to send later, on your existing infrared remote. The program then will print out the protocol, address and command discovered.

## Libraries used and licenses

The software uses several open source libraries with different licenses.

The HAL library from ST is published under the BSD 3-Clause license.

Some CMSIS header files are published under the Apache 2.0 license.

The [IRMP](https://www.mikrocontroller.net/articles/IRMP) project is published under GPL 2.0 or later.

[Libusb_stm32](https://github.com/dmitrystu/libusb_stm32) is published under the Apache 2.0 license.

Since GPL 2.0 and Apache 2.0 are considered [incompatible](https://www.apache.org/licenses/GPL-compatibility.html) by the Free Software Foundation, but GPL 3.0 is considered compatible. GPL 3.0 is the only possible license for the firmware on the microcontroller side.

The PCB is under creative common license. [CC-BY-SA 3.0 DE](https://creativecommons.org/licenses/by-sa/3.0/de/deed.en)
