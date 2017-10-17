# Package signer

The scripts in this folder can create certificates, and sign firmware packages.

## Prerequisites

* node.js (8 or higher)
* OpenSSL

## Generating keys

To sign packages you need a public/private key pair. The public key is embedded in your device, and the private key can be used to sign new firmware packages. In addition, a device has a manufacturer UUID and a device class UUID. Firmware packages are tagged with these UUIDs to prevent flashing an incompatible firmware on device.

To generate a new set of keys, run:

```
$ node generate-keys.js your-organisation-domain device-class-name
```

## Signing packages

To sign a new package, run:

```
$ node sign-package.js path-to-your_application.bin > signed_application.bin
```

## Keys

A firmware needs to be signed with a private key. You find the keys in the `certs` directory. The public key needs to be included in the device firmware (in the `update_certs.h` file), which uses the key to verify that the firmware was signed with the private key.

To create new keys, run:

```
$ openssl genrsa -out certs/update.key 2048
$ openssl rsa -pubout -in certs/update.key -out update.pub
```

Firmware is also tagged with a device manufacturer UUID and device class UUID, used to prevent flashing the wrong application.

## Generating an application package

1. Compile an image for Multi-Tech xDot with bootloader enabled (the same bootloader as in this project, so the offsets are correct).
1. Copy the `_application.bin` file into this folder.
1. Run:

    ```
    $ npm install
    $ node create-packets-h.js my-app_application.bin ../src/packets.h
    ```

1. This command creates the `packets.h` and the `update_certs.h` files.
1. Re-compile lorawan-fragmentation-in-flash and see the xDot update to your new application.
