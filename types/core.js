'use strict';

const CLI = require('@fabric/core/types/cli');
const Verse = require('./verse');

class Core extends CLI {
  /**
   * Primary unit for processing.
   * @param  {Configuration} config Map of settings.
   * @return {Core}        Instance of the {@link Core}.
   */
  constructor (config) {
    super(config);

    this.config = Object.assign({
      name: 'Verse',
      services: ['http'],
      http: {
        port: 7676
      }
    }, config);

    // ensure type `Core`
    if (!(this instanceof Core)) return new Core(this.config);

    this.verse = new Verse();
    this.status = 'ready';

    return this;
  }

  async start () {
    this.log('Starting…');

    // first, agree upon vector definitions
    await this.verse.define('Vector', require('../resources/vector'));
    await this.verse.define('Person', require('../resources/person'));

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
