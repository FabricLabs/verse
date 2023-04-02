'use strict';

const Service = require('@fabric/core/types/service');

class Space extends Service {
  constructor (settings = {}) {

    this.settings = Object.assign({
      state: {}
    }, settings);

    this._state = {
      content: this.settings.state
    };

    return this;
  }
}

module.exports = Space;
