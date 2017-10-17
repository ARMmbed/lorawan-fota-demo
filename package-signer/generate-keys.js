const execSync = require('child_process').execSync;
const UUID = require('uuid-1345');
const fs = require('fs');
const Path = require('path');

const certsFolder = Path.join(__dirname, 'certs');

if (fs.existsSync(certsFolder)) {
    console.error(`'certs' folder already exists, refusing to overwrite existing certificates`);
    process.exit(1);
}

fs.mkdirSync('certs');

execSync(`openssl genrsa -out certs/update.key 2048`)
$ openssl rsa -pubout -in certs/update.key -out update.pub