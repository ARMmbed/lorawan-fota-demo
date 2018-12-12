# This repository is superseded by [mbed-os-example-lorawan-fuota](https://github.com/armmbed/mbed-os-example-lorawan-fuota) which implements the latest specifications

This application implements multicast firmware updates over LoRaWAN. It runs on Multi-Tech xDot devices with external flash (AT45 SPI flash by default) like the [L-TEK FF1705](https://os.mbed.com/platforms/L-TEK-FF1705/).

The application implements:

* LoRaWAN Data block proposal - including forward error correction.
* LoRaWAN Multicast proposal.
* External flash support for storing firmware update fragments.
* Binary patching for delta updates.
* Bootloader support to perform the actual firmware update.

## How to build

1. Create an account at the [experimental The Things Network FOTA](https://console.fotademo.thethings.network) deployment.
1. Point your gateway to `router.fotademo.thethings.network`.
1. Create a new device in the The Things Network console.
1. Set your application EUI and application key in `main.cpp`.
1. Install Mbed CLI and either ARMCC 5 or GCC ARM 6 (not 4.9!), and import this application:

    ```
    $ mbed import https://github.com/armmbed/lorawan-fota-demo
    ```

1. Generate a set of keys to sign updates:

    ```
    $ cd package-signer
    $ npm install
    $ node generate-keys.js your-domain.com your-device-model
    ```

1. Compile this application:

    ```
    # ARMCC
    $ mbed compile -m xdot_l151cc -t ARM --profile=./profiles/develop.json

    # GCC
    $ mbed compile -m xdot_l151cc -t GCC_ARM --profile=./profiles/develop.json
    ```

1. Flash the application on your development board.

## Relevant projects

* [Bootloader](https://github.com/armmbed/lorawan-fota-bootloader)
* [Fragmentation library](https://github.com/janjongboom/mbed-lorawan-frag-lib)
* [Patching library](https://github.com/janjongboom/janpatch)
* [Standalone fragmentation demonstration](https://github.com/janjongboom/lorawan-fragmentation-in-flash) - useful when developing, as you don't need a LoRa network.

## Other flash drivers

If you're using a different flash chip, you'll need to implement the [BlockDevice](https://docs.mbed.com/docs/mbed-os-api-reference/en/latest/APIs/storage/block_device/) interface. See `AT45BlockDevice.h` in the `at45-blockdevice` driver for more information.

## Update keys

Updates need to be signed using ECDSA/SHA256. The private key is held by the manufacturer of the device, whilst the public key is baked into the device firmware. When an update comes in the signature is verified by the public key. In addition, firmware is tagged with the manufacturer UUID and the device model UUID. These are also baked into the device firmware. This is a prevention mechanism designed to avoid flashing incompatible firmware to devices.

To generate a new key pair, and to generate the UUIDs, run (requires OpenSSL):

```
$ node package-signer/generate-keys.js your-domain.com your-device-model
```

### Update format

The update file format is:

1. 1 byte, size of the signature (70, 71 or 72 bytes).
1. 72 bytes, ECDSA/SHA256 signature of the update file. In case of a patch file, this is the signature of the file *after* patching (thus it's also a way of checking if patching succeeded). If the signature is smaller than 72 bytes, right pad with `00`.
1. 16 bytes, manufacturer UUID.
1. 16 bytes, device class UUID.
1. 1 byte, diff indication. If `0`, then this is not a delta update. If `1` it's a delta update.
1. 3 bytes, size of current firmware (if delta update). If sending a delta update then this field indicates the size of the current (before patching) firmware.
1. The update file (either diff or full image).

### Creating update file

These scripts uses the keys / device IDs in the `package-signer/certs` folder.

**Full update**

A full update file can be created via:

```
$ node package-signer/sign-package.js BUILD/PATH/TO/BINARY_application.bin > full-new-fw.bin
```

**Diff update**

A diff update file can be created via:

```
$ node package-signer/create-and-sign-diff.js OLD_FILE_application.bin NEW_FILE_application.bin > diff-new-fw.bin
```

Make sure to tag the applications with a version number, and store them somewhere, to make your life significantly easier.
