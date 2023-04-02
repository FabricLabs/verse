'use strict';

const Service = require('@fabric/core/types/service');

class Place extends Service {
  constructor (settings = {}) {
    super(settings);

    this.settings = Object.assign({
      // Import all supplied parameters to state
      state: Object.assign({
        status: 'PAUSED',
        name: settings.name || 'Uninitialized Region',
        description: 'This devoid region of space remains untouched.'
      }, settings)
    }, settings);

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  get name () {
    return this.state.name;
  }

  toHTML () {
    return `
      <verse-place class="ui card">
        <div class="content">
          <header class="header">${this.name}</header>
          <p>${this.description}</p>
        </div>
      </verse-place>
    `.trim();
  }
}

module.exports = Place;
