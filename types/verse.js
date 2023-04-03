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
    this.placeQueue = {};

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

    // Field deletions
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

  toHTML () {
    return [
      `<fabric-application>`,
        `<verse-instance class="ui card">`,
          `<header class="header">${this.name}</header>`,
          `<p class="content">${this.description}</p>`,
        `</verse-instance>`,
      `</fabric-application>`
    ].join('');
  }

  _unsyncedLocations () {
    const d = Object.values(this.state.paths).map(x => x.to);

    const d = Object.values(this.state.paths).map(x => x.to);
    const filtered = d.filter((x) => {
      const target = new Actor({ _id: x });
      if (!this.state.places[target.id]) {
        return true;
      } else {
        return false;
      }
    });

    return new Set(filtered);
  }

  async start () {
    await this._loadFromRPG();
    this._state.content.status = 'STARTED';
    this.commit();
  }

  async tick () {
    this._state.content.clock++;
    await this._syncMissingPaths();
    await this._syncOldestPlaces();
    await this._syncRandomPlaces();
    this.prune();
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

  async _syncAllPaths () {
    for (const key of Object.keys(this.state.paths)) {
      const path = this.state.paths[key];
      const from = this.registerPlace({ _id: path.from });
      const to = this.registerPlace({ _id: path.to });
    }
  }

  async _syncMissingPaths () {
    const unsynced = this._unsyncedLocations();
    const queue = Array.from(unsynced);


    for (let i = 0; (i < 10 && i < queue.length); i++) {
      await this._syncPlaceID(queue.shift());
    }
  }

  async _syncOldestPlaces () {
    const oldest = Object.keys(this.state.places).sort((a, b) => {
      if (!this._pongs[a]) return -1;
      if (!this._pongs[b]) return 1;
      if (this._pongs[a] > this._pongs[b]) return 1;
      if (this._pongs[a] <= this._pongs[b]) return -1;
      return 0;
    }).slice(0, 10).map((id) => {
      return {
        _id: this.state.places[id]._id,
        name: this.state.places[id].name,
        age: Date.now() - (this._pongs[id] || 0)
      }
    });

    for (let i = 0; i < oldest.length; i++) {
      await this._syncPlaceID(oldest[i]._id);
    }
  }

  /**
   * Import a location's latest data from RPG.
   * @param {Number} _id RPG Place ID.
   * @returns {Place|Error} Place or error.
   */
  async _syncPlaceID (_id) {
    if (!_id) return console.trace('No place ID provided');
    return new Promise((resolve, reject) => {
      // Get place ID (internal)
      const id = this.registerPlace({ _id });
      this.rpg._GET(`/places/${_id}`).catch((exception) => {
        this.emit('debug', `Could not sync place: ${exception}`);
        reject(exception);
      }).then((entity) => {
        if (!entity) return reject(new Error(`Place ID # ${_id} did not return a result: ${entity}`));

        this._state.content.places[id].name = entity.name;
        this._state.content.places[id].slug = entity.slug;
        this._state.content.places[id].synopsis = entity.synopsis;
        this._state.content.places[id].exits = entity.exits;

        for (const character of entity.characters) {
          const c = this.registerCharacter({ _id: character.id });
          this._state.content.characters[c].name = character.name;
          this._state.content.characters[c].slug = character.url;
          this._state.content.characters[c].location = character.location;
          this._pongs[c] = Date.now();
        }

        for (const exit of entity.exits) {
          this.registerPath({
            direction: exit.direction,
            from: _id,
            to: exit.destination
          });
          // await this._syncPlaceID(exit.destination);
        }

        this.rpg._GET(`/places/${_id}/characters`).then((characters) => {
          for (const character of characters) {
            if (character.universe !== 1) continue;
            const c = this.registerCharacter({ _id: character.id });
            this._state.content.characters[c].name = character.name;
            this._state.content.characters[c].slug = character.url;
            this._state.content.characters[c].location = character.location;
            this._pongs[c] = Date.now();
          }

          this._pongs[id] = Date.now();
          this.commit();

          resolve(this._state.content.places[id]);
        });
      });
    });
  }

  async _syncRandomPlaces () {
    if (!this._RPGPlaceIDs.length) return;
    for (let i = 0; i < 10; i++) {
      const id = Math.floor(Math.random() * this._RPGPlaceIDs.length);
      await this._syncPlaceID(this._RPGPlaceIDs[id]);
    }
  }
}

module.exports = Verse;
