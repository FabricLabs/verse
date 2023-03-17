'use strict';

const Engine = require('../types/core');
const DEFAULTS = require('../config');

export default class Verse extends Engine {
  /**
   * Fibre in the {@link Fabric}.
   * @param  {Configuration} config
   * @return {Promise}
   */
  constructor (config) {
    super(config);
    this.config = Object.assign({}, DEFAULTS);
    this.engine = new Engine(this.config);
    this.status = 'installed';
    return this.engine.verse.start();
  }
}

window.verse = new Verse(DEFAULTS);
module.exports = Verse;
