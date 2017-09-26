# Firmware Updates over LoRaWAN for Multi-Tech xDot

This application implements multicast firmware updates over LoRaWAN. It runs on Multi-Tech xDot devices with external flash (AT45 SPI flash by default) like the [L-TEK FF1705](https://os.mbed.com/platforms/L-TEK-FF1705/).

The application implements:

* LoRaWAN Data block proposal - including forward error correction.
* LoRaWAN Multicast proposal.
* External flash support for storing firmware update fragments.
* Bootloader support to perform the actual firmware update.

**Note:** This application is not open sourced yet. Please do not share without permission.

## How to build

1. Get access to The Things Network Canary, so you can actually deploy updates.
1. Set your application EUI and application key in `main.cpp`.
1. Install Mbed CLI and either ARMCC 5 or GCC ARM 6 (not 4.9!), and import this application:

    ```
    $ mbed import https://github.com/armmbed/fota-lora-radio
    ```

1. Compile this application:

    ```
    # ARMCC
    $ mbed compile -m xdot_l151cc -t ARM

    # GCC
    $ mbed compile -m xdot_l151cc -t GCC_ARM
    ```

1. Flash the application on your development board.

## Relevant projects

* [Bootloader](https://github.com/janjongboom/lorawan-at45-fota-bootloader)
* [Fragmentation library](https://github.com/janjongboom/mbed-lorawan-frag-lib)
* [Standalone fragmentation demonstration](https://github.com/janjongboom/lorawan-fragmentation-in-flash) - useful when developing, as you don't need a LoRa network.

## Other flash drivers

If you're using a different flash chip, you'll need to implement the [BlockDevice](https://docs.mbed.com/docs/mbed-os-api-reference/en/latest/APIs/storage/block_device/) interface. See `AT45BlockDevice.h` in the `at45-blockdevice` driver for more information.

## More information

Contact Jan Jongboom, jan.jongboom@arm.com.

