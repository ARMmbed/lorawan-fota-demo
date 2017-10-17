const fs = require('fs');
const Path = require('path');
const execSync = require('child_process').execSync;
const UUID = require('uuid-1345');
const deviceId = require('./certs/device-ids');

let manufacturerUUID = new UUID(deviceId['manufacturer-uuid']).toBuffer();
let deviceClassUUID = new UUID(deviceId['device-class-uuid']).toBuffer();

// diff info contains (bool is_diff, 3 bytes for the size of the *old* firmware)
let isDiffBuffer = Buffer.from([ 0, 0, 0, 0 ]);

const binaryPath = Path.resolve(process.argv[2]);
const tempFilePath = Path.join(__dirname, 'temp.bin');

// now we need to create a signature...
let signature = execSync(`openssl dgst -sha256 -sign ${Path.join(__dirname, 'certs', 'update.key')} ${binaryPath}`);
console.error('Signed signature is', signature.toString('hex'));

// now make a temp file which contains signature + class IDs + if it's a diff or not + bin
process.stdout.write(Buffer.concat([ signature, manufacturerUUID, deviceClassUUID, isDiffBuffer, fs.readFileSync(binaryPath) ]));

