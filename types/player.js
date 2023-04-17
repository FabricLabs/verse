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
    }, settings);

    this.rpg = new Remote({ host: 'www.roleplaygateway.com' });
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

  async _getCharacters () {
    const characters = await this.remote._GET(`/players/${this.user.id}/characters`);
    characters.forEach((character) => {
      this.emit('character', character);
    });
  }

  async _loadFromCharacter (id) {
    const character = await this.remote._GET(`/characters/${id}`);

    this._state.content._id = character._id;
    this._state.content.name = character.name;
    this._state.content.slug = character.slug;
    this._state.content.synopsis = character.synopsis;
    this._state.content.location = character.location;

    this.commit();

    return this;
  }

  async _loginRPG (username, password) {
    try {
      const result = await this.rpg._POST('/services/identity', JSON.stringify({
        user: {
          id: `@${username}:roleplaygateway.com`,
          password: password
        }
      }));

      const status = (result && result.auth && result.auth.success) ? true : false;

      if (status) {
        this.user = {
          id: parseInt(result.auth.id.value),
          name: result.auth.profile.display_name
        };
      }

      return status;
    } catch (exception) {
      console.error('could not login:', exception);
    }

    return false;
  }
}

module.exports = Player;
