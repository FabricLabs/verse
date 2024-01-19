'use strict';

const Actor = require('@fabric/core/types/actor');
const Remote = require('@fabric/http/types/remote');

class Place extends Actor {
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

    this.remote = new Remote({ host: 'api.roleplaygateway.com' });

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  set name (value) {
    this._state.content.name = value;
  }

  get name () {
    return this.state.name;
  }

  get _RPGExitDestinationIDs () {
    return [];
  }

  isSurroundedByRPGPlaceID (target) {
    if (!target) return false;
    if (!this._RPGExitDestinationIDs.includes(target)) return false;
    // all normal exits lead to target
    return true;
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

  async _loadFromRPGByID (id) {
    const place = await this.remote._GET(`/places/${id}`);
    this._state.content._id = place._id;
    this._state.content.name = place.name;
    this._state.content.synopsis = place.synopsis;
    this.commit();
    return this;
  }
}

module.exports = Place;
