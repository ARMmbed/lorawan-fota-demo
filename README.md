# Firmware Updates over LoRaWAN for Multi-Tech xDot

To be used by a Multi-Tech xDot with AT45 external flash.

## Bootloader

https://github.com/janjongboom/lorawan-at45-fota-bootloader

## More info

Ask Jan Jongboom.

## To build

First install mbed CLI and GCC ARM Embedded 4.9.3.

```
$ mbed deploy
$ mbed compile -m xdot_l151cc -t GCC_ARM
```
