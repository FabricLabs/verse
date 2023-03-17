'use strict';

const Actor = require('@fabric/core/types/actor');
const Remote = require('@fabric/core/types/remote');
const Service = require('@fabric/core/types/service');

class Verse extends Service {
  constructor (settings = {}) {
    super(settings);

    this.settings = Object.assign({
      state: {
        clock: 0,
        characters: {},
        places: {},
        players: {},
        status: 'PAUSED',
        title: null
      }
    }, settings);

    this.rpg = new Remote({ authority: 'api.roleplaygateway.com' });

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  registerCharacter (character) {
    const actor = new Actor(character);
    if (!this._state.content.characters) this._state.content.characters = {};
    this._state.content.characters[actor.id] = character;
    return actor.id;
  }

  registerPlayer (player) {
    const actor = new Actor(player);
    if (!this._state.content.players) this._state.content.players = {};
    this._state.content.players[actor.id] = player;
    return actor.id;
  }

  registerPlace (place) {
    const actor = new Actor(place);
    if (!this._state.content.places) this._state.content.places = {};
    this._state.content.places[actor.id] = place;
    return actor.id;
  }

  tick () {
    this._state.content.clock++;
    this.commit();
  }

  async start () {
    await this._loadFromRPG();
    this._state.content.status = 'STARTED';
    this.commit();
  }

  async _loadFromRPG () {
    const universe = await this.rpg._GET('/universes/1');

    // Universe properties
    this._state.content.title = universe.title;
    this._state.content.slug = universe.slug;
    this._state.content.created = (new Date('2005-07-01 00:00:00')).toISOString();

    // Permissions
    for (const master of universe.permissions.masters) {
      this.registerPlayer({ _id: master._id });
    }

    for (const builder of universe.permissions.builders) {
      this.registerPlayer({ _id: builder._id });
    }

    // Players
    for (const player of universe._players) {
      const id = this.registerPlayer({ _id: player._id });
      this._state.content.players[id].name = player.username;
    }

    // Places
    for (const place of universe._places) {
      await this._syncPlaceID(place.id);
    }

    this.commit();
  }

  async _syncPlaceID (_id) {
    const id = this.registerPlace({ _id });
    const entity = await this.rpg._GET(`/places/${_id}`);

    for (const character of entity.characters) {
      const c = this.registerCharacter({ _id: character.id });
      this._state.content.characters[c].name = character.name;
      this._state.content.characters[c].slug = character.url;
    }

    this._state.content.places[id].name = entity.name;
    this._state.content.places[id].slug = entity.slug;
    this._state.content.places[id].synopsis = entity.synopsis;
    this._state.content.places[id].exits = entity.exits;

    this.commit();
  }
}

module.exports = Verse;
