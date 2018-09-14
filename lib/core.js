'use strict';

const Verse = require('./verse');
const defaults = require('../config');

class Core extends Verse {
  constructor (config) {
    super(config);

    this.config = Object.assign({
      name: 'Verse',
      services: ['http'],
      http: {
        port: 7676
      }
    }, defaults);

    this.verse = new Verse();
    this.status = 'ready';

    this.commit();

    return this;
  }

  async start () {
    this.log('Starting…');

    // first, agree upon vector definitions
    await this.verse.define('Vector', require('./resources/vector'));
    await this.verse.define('Person', require('./resources/person'));

    this.engine = this.verse.start();
    this.input = this.verse.prompt();
    this.output = this.verse.draw();

    this.warn('This module is untested.  Proceed at your own risk.');
    this.commit();

    return this;
  }
}

// Make a Promise
module.exports = Core;
