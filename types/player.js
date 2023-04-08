'use strict';

const Actor = require('@fabric/core/types/actor');
const Remote = require('@fabric/http/types/remote');

class Player extends Actor {
  constructor (settings = {}) {
    super(settings);

    this.settings = Object.assign({
      state: {
        status: 'PAUSED'
      }
    });

    this.remote = new Remote({
      host: 'api.roleplaygateway.com'
    });

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  _request (path) {
    this.emit('request', {
      object: { path }
    });
  }

  async _loadFromCharacter (id) {
    const character = await this.remote._GET(`/characters/${id}`);
    console.log('character:', character);
    this._state.content._id = character._id;
    this._state.content.name = character.name;
    this._state.content.slug = character.slug;
    this._state.content.synopsis = character.synopsis;
    this._state.content.location = character.location;
    this.commit();
    return this;
  }
}

module.exports = Player;
