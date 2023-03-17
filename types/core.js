'use strict';

// Fabric Core
const Actor = require('@fabric/core/types/actor');
const Filesystem = require('@fabric/core/types/filesystem');
const Peer = require('@fabric/core/types/peer');
const Service = require('@fabric/core/types/service');

const _sortKeys = require('@fabric/core/functions/_sortKeys');

// Fabric HTTP
const HTTPServer = require('@fabric/http/types/server');

// Verse
const Verse = require('./verse');

/**
 * Core service for running VERSE.
 */
class Core extends Service {
  /**
   * Primary unit for processing.
   * @param  {Object} settings Map of settings.
   * @return {Core} Instance of the {@link Core}.
   */
  constructor (settings = {}) {
    super(settings);

    this.settings = Object.assign({
      name: 'Verse',
      clock: 0,
      interval: 60000,
      port: 9999,
      services: ['http'],
      state: {
        status: 'PAUSED',
        verse: null
      },
      fs: {
        path: `${process.env.HOME}/.verse/core`
      },
      http: {
        port: 7676
      },
      identity: null
    }, settings);

    // ensure type `Core`
    if (!(this instanceof Core)) return new Core(this.settings);

    // Internal Services
    this.fs = new Filesystem(this.settings.fs);
    this.http = new HTTPServer(this.settings.http);
    this.node = new Peer(this.settings);

    // Local Actor
    this.actor = new Actor(this.settings.identity);

    // Create the Verse
    this.verse = new Verse();
    this.verse.on('commit', this._handleVerseCommit.bind(this));

    this.ticker = null;

    // Fabric State
    this._state = {
      content: this.settings.state
    };

    // Set status
    this.status = 'ready';

    return this;
  }

  commit () {
    const sorted = _sortKeys(this.state);
    this.fs.publish('STATE', JSON.stringify(sorted, null, '  '));
    super.commit();
  }

  tick () {
    this.clock++;
    this.verse.tick();

    this._state.content = Object.assign({}, this.state, {
      verse: _sortKeys(this.verse.state)
    });

    this.commit();
  }

  _handleVerseCommit (commit) {
    this._state.content.verse = commit.state;
    this.commit();
  }

  async start () {
    this.emit('log', 'Starting…');

    // Load from filesystem
    await this.fs.start();

    const file = this.fs.readFile('STATE');
    const state = (file) ? JSON.parse(file) : this.state;

    // Import local state
    this._state.content = _sortKeys(state);
    this.verse._state.content = state.verse || this.verse.state;

    // Start services
    await this.verse.start();
    await this.http.start();

    // Update state
    this._state.content.status = 'STARTED';

    // Commit
    this.commit();

    this.emit('debug', `Tick interval: ${this.settings.interval}`);
    this.ticker = setInterval(() => {
      this.tick();
    }, this.settings.interval);

    return this;
  }
}

// Make a Promise
module.exports = Core;
