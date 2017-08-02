# LoRa FOTA WIP

This is the LoRa radio part (mDot or xDot).

Needs target MCU running https://github.com/ARMmbed/update-client-wo-cloud/tree/lora, or need to implement https://github.com/janjongboom/lorawan-fragmentation-in-flash.

Connect radio and target MCU over UART D1/D0.

## More info

Ask Jan Jongboom.

## To build

First install mbed CLI and GCC ARM Embedded 4.9.3.

```
$ git clone git@github.com:armmbed/fota-lora-radio.git
$ cd fota-lora-radio
$ mbed deploy
$ mbed compile -m xdot_l151cc -t GCC_ARM
```
