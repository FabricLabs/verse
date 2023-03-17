'use strict';

const Service = require('@fabric/core/types/service');

class Verse extends Service {
  constructor (settings = {}) {
    super(settings);

    this.settings = Object.assign({
      state: {
        clock: 0,
        status: 'PAUSED'
      }
    }, this.settings, settings);

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  tick () {
    this._state.content.clock++;
    this.commit();
  }
}

module.exports = Verse;
