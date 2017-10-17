const fs = require('fs');
const Path = require('path');
const execSync = require('child_process').execSync;
const UUID = require('uuid-1345');
const crypto = require('crypto');
const deviceId = require('./certs/device-ids');

let manufacturerUUID = new UUID(deviceId['manufacturer-uuid']).toBuffer();
let deviceClassUUID = new UUID(deviceId['device-class-uuid']).toBuffer();

const oldPath = Path.resolve(process.argv[2]);
const newPath = Path.resolve(process.argv[3]);
const oldFile = fs.readFileSync(oldPath);
const newFile = fs.readFileSync(newPath);

const oldSize = fs.statSync(oldPath).size;

// console.error('oldSize is', oldSize);

// create diff...
let diff = execSync(`~/Downloads/jdiff081/src/jdiff ${oldPath} ${newPath}`);

let oldHash = crypto.createHash('sha256').update(oldFile).digest('hex');
let diffHash = crypto.createHash('sha256').update(diff).digest('hex');
let newHash = crypto.createHash('sha256').update(newFile).digest('hex');

// diff info contains (bool is_diff, 3 bytes for the size of the *old* firmware)
let isDiffBuffer = Buffer.from([ 1, oldSize >> 16 & 0xff, oldSize >> 8 & 0xff, oldSize & 0xff ]);

console.error('old hash: ', oldHash);
console.error('diff hash:', diffHash);
console.error('new hash: ', newHash);
console.error('');

// we need to create signature based on the *new file*
let signature = execSync(`openssl dgst -sha256 -sign ${Path.join(__dirname, 'certs', 'update.key')} ${newPath}`);
console.error('Signed signature is', signature.toString('hex'));

let sigLength = Buffer.from([ signature.length ]);

if (signature.length === 70) {
    signature = Buffer.concat([ signature, Buffer.from([ 0, 0 ]) ]);
}
else if (signature.length === 71) {
    signature = Buffer.concat([ signature, Buffer.from([ 0 ]) ]);
}

// now make a temp file which contains signature + class IDs + if it's a diff or not + bin
process.stdout.write(Buffer.concat([ sigLength, signature, manufacturerUUID, deviceClassUUID, isDiffBuffer, diff ]));
