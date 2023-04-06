'use strict';

const Actor = require('@fabric/core/types/actor');
const Remote = require('@fabric/core/types/remote');
const Service = require('@fabric/core/types/service');

const Universe = require('./universe');

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
      },
      universe: {
        id: 1
      }
    }, settings);

    this.universe = new Universe(this.settings);
    this.rpg = new Remote({ authority: 'api.roleplaygateway.com' });
    this.placeQueue = {};
    this.motds = [
      'YOLO!'
    ];

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  get motd () {
    return this.motds[Math.floor(Math.random() * this.motds.length)]
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

  async start () {
    await this.universe.start();
    this._state.content.status = 'STARTED';
    this.commit();
    console.log('[VERSE] MOTD:', this.motd);
    return this;
  }

  async tick () {
    const now = new Date();
    this._state.content.clock++;

    await this.universe._syncMissingPaths();
    await this.universe._syncOldestPlaces();
    await this.universe._syncRandomPlaces();
    // this.universe.prune();

    const state = new Actor(this.state);

    this.emit('state', this.state);
    this.emit('tick', {
      clock: this.state.clock,
      created: now.toISOString(),
      state: state.id
    });

    this.commit();
  }
}

module.exports = Verse;
