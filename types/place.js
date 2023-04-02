'use strict';

const Service = require('@fabric/core/types/service');

class Place extends Service {
  constructor (settings = {}) {
    super(settings);

    this.settings = Object.assign({
      state: {
        status: 'PAUSED',
        name: 'Uninitialized Region'
      }
    }, settings);

    this._state = {
      content: this.settings.state
    };

    return this;
  }
}

module.exports = Place;
