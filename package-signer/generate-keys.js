const execSync = require('child_process').execSync;
const UUID = require('uuid-1345');
const fs = require('fs');
const Path = require('path');

let org = process.argv[2];
let deviceClass = process.argv[3];

if (!org || !deviceClass) {
    console.log('Usage: generate-keys.js organization device-class');
    process.exit(1);
}

const certsFolder = Path.join(__dirname, 'certs');

if (fs.existsSync(certsFolder)) {
    console.log(`'certs' folder already exists, refusing to overwrite existing certificates`);
    process.exit(1);
}

fs.mkdirSync('certs');

console.log('Creating keypair');

execSync(`openssl ecparam -genkey -name secp256r1 -out ${Path.join(certsFolder, 'update.key')}`)
execSync(`openssl ec -in ${Path.join(certsFolder, 'update.key')} -pubout > ${Path.join(certsFolder, 'update.pub')}`);

let pubKey = fs.readFileSync(Path.join(certsFolder, 'update.pub'), 'utf-8');

console.log('Creating keypair OK');

let deviceIds = {
    'manufacturer-uuid': UUID.v5({
        namespace: UUID.namespace.url,
        name: org
    }),
    'device-class-uuid': UUID.v5({
        namespace: UUID.namespace.url,
        name: deviceClass
    })
};

fs.writeFileSync(Path.join(certsFolder, 'device-ids.js'), `module.exports = ${JSON.stringify(deviceIds, null, 4)};`, 'utf-8');

console.log('Wrote device-ids.js OK');

// now create the .H file...
let manufacturerUUID = new UUID(deviceIds['manufacturer-uuid']).toBuffer();
let deviceClassUUID = new UUID(deviceIds['device-class-uuid']).toBuffer();

let certs = `#ifndef _UPDATE_CERTS_H
#define _UPDATE_CERTS_H

const char * UPDATE_CERT_PUBKEY = ${JSON.stringify(pubKey)};
const size_t UPDATE_CERT_LENGTH = ${pubKey.length + 1};

const uint8_t UPDATE_CERT_MANUFACTURER_UUID[16] = { ${Array.from(manufacturerUUID).map(c => '0x' + c.toString(16)).join(', ')} };
const uint8_t UPDATE_CERT_DEVICE_CLASS_UUID[16] = { ${Array.from(deviceClassUUID).map(c => '0x' + c.toString(16)).join(', ')} };

#endif // _UPDATE_CERTS_H_
`;

console.log('Writing UpdateCerts.h');
fs.writeFileSync(Path.join(certsFolder, 'UpdateCerts.h'), certs, 'utf-8');
fs.writeFileSync(Path.join(__dirname, '..', 'src', 'UpdateCerts.h'), certs, 'utf-8');
console.log('Writing UpdateCerts.h OK');
