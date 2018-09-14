'use strict';

const Verse = require('./lib/verse');
const config = require('./config');

async function main () {
  this.config = Object.assign({
    name: 'Verse',
    services: ['http'],
    http: {
      port: 7676
    }
  }, config);

  this.verse = new Verse();

  // first, agree upon vector definitions
  this.verse.define('Vector', require('./resources/vector'));
  this.verse.define('Person', require('./resources/person'));

  this.engine = this.verse.start();
  this.input = this.verse.prompt();
  this.output = this.verse.draw();

  this.warn('This module is untested.  Proceed at your own risk.');

  return this;
}

// Make a Promise
module.exports = main();
