'use strict';

const Service = require('@fabric/core/types/service');

const Chunk = require('./chunk');

class Space extends Service {
  constructor (settings = {}) {

    this.settings = Object.assign({
      dimensions: {
        x: 32,
        y: 32,
        z: 32
      },
      state: {}
    }, settings);

    this.chunks = [];

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  appendChunk (chunk) {
    if (chunk) chunk = new Chunk();
    this.chunks.push(chunk);
    return this;
  }

  load () {
    if (!this.chunks.length) this.appendChunk();
    return this;
  }
}

module.exports = Space;
