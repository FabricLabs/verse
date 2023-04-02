'use strict';

const Actor = require('@fabric/core/types/actor');
const Remote = require('@fabric/core/types/remote');
const Service = require('@fabric/core/types/service');

class Verse extends Service {
  constructor (settings = {}) {
    super(settings);

    this.settings = Object.assign({
      constraints: {
        places: {
          count: 10
        }
      },
      state: {
        clock: 0,
        characters: {},
        paths: {},
        places: {},
        players: {},
        status: 'PAUSED',
        title: null
      }
    }, settings);

    this.rpg = new Remote({ authority: 'api.roleplaygateway.com' });

    this._pongs = {};

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  get characterIDs () {
    return Object.keys(this.state.characters);
  }

  get pathIDs () {
    return Object.keys(this.state.paths);
  }

  get placeIDs () {
    return Object.keys(this.state.places);
  }

  get playerIDs () {
    return Object.keys(this.state.players);
  }

  get _RPGPlaceIDs () {
    return Object.values(this.state.places).map((x) => {
      return x._id;
    });
  }

  prune () {
    const overage = Object.keys(this.state.places).length - this.settings.constraints.places.count;

    for (const id of Object.keys(this.state.places)) {
      delete this._state.content.places[id].synced;
    }

    for (const id of Object.keys(this.state.characters)) {
      delete this._state.content.characters[id].synced;
    }

    this.commit();
  }

  registerCharacter (character) {
    const actor = new Actor(character);
    if (!this._state.content.characters) this._state.content.characters = {};
    if (this.state.characters[actor.id]) return actor.id;
    this._state.content.characters[actor.id] = character;
    return actor.id;
  }

  registerPath (path) {
    const actor = new Actor(path);
    if (!this._state.content.paths) this._state.content.paths = {};
    if (this.state.paths[actor.id]) return actor.id;
    this._state.content.paths[actor.id] = path;
    return actor.id;
  }

  registerPlayer (player) {
    const actor = new Actor(player);
    if (!this._state.content.players) this._state.content.players = {};
    if (this.state.players[actor.id]) return actor.id;
    this._state.content.players[actor.id] = player;
    return actor.id;
  }

  registerPlace (place) {
    const actor = new Actor(place);
    if (!this._state.content.places) this._state.content.places = {};
    if (this.state.places[actor.id]) return actor.id;
    this._state.content.places[actor.id] = place;
    return actor.id;
  }

  tick () {
    this._state.content.clock++;
    this.prune();
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
    this._state.content.created = '2005-07-01T04:00:00.000Z';

    // Game Masters
    for (const master of universe.permissions.masters) {
      this.registerPlayer({ _id: master._id });
    }

    // Builders
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

    this._state.content.places[id].name = entity.name;
    this._state.content.places[id].slug = entity.slug;
    this._state.content.places[id].synopsis = entity.synopsis;
    this._state.content.places[id].exits = entity.exits;

    for (const character of entity.characters) {
      const c = this.registerCharacter({ _id: character.id });
      this._state.content.characters[c].name = character.name;
      this._state.content.characters[c].slug = character.url;
    }

    for (const exit of entity.exits) {
      this.registerPath({ direction: exit.direction, from: _id, to: exit.destination });
      // await this._syncPlaceID(exit.destination);
    }

    this.commit();
  }
}

module.exports = Verse;
