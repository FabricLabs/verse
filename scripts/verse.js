'use strict';

const Engine = require('../lib/core');
const config = require('../config');

async function main () {
  return Engine(config).verse.start();
}

// Make a Promise
module.exports = main();
