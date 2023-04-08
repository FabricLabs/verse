'use strict';

// Dependencies
const monitor = require('fast-json-patch');

// Fabric Types
const Actor = require('@fabric/core/types/actor');
const Filesystem = require('@fabric/core/types/filesystem');
const Message = require('@fabric/core/types/message');
const Peer = require('@fabric/core/types/peer');
const Service = require('@fabric/core/types/service');
const Signer = require('@fabric/core/types/signer');

// Fabric Functions
// Ensures consistency of property order
const _sortKeys = require('@fabric/core/functions/_sortKeys');

// Fabric HTTP
const HTTPServer = require('@fabric/http/types/server');

// Engine
const Verse = require('./verse');

/**
 * Core service for running VERSE.
 */
class Core extends Service {
  /**
   * Primary unit for processing.
   * @param  {Object} [settings] Map of settings.
   * @param  {String} [settings.contract] Existing contract ID in network.
   * @param  {Object} [settings.state] State to adopt.
   * @return {Core} Instance of the {@link Core}.
   */
  constructor (settings = {}) {
    super(settings);

    // Settings
    this.settings = Object.assign({
      alias: '@verse/core',
      name: 'VERSE', // Sets Fabric Primitive
      contract: null, // ID of the deployed contract
      clock: 0,
      interval: 600000, // 10 minutes
      listen: true,
      networking: true,
      peers: [
        'hub.fabric.pub:7777'
      ],
      port: 9999,
      services: ['http'],
      state: {
        clock: 0,
        status: 'PAUSED',
        verse: null
      },
      fs: {
        path: `${process.env.HOME}/.verse/core`
      },
      http: {
        port: 7676
      },
      identity: {
        seed: null
      }
    }, settings);

    // ensure type `Core`
    if (!(this instanceof Core)) return new Core(this.settings);

    // Internal Services
    this.fs = new Filesystem(this.settings.fs);
    this.http = new HTTPServer(this.settings.http);
    this.signer = new Signer(this.settings.identity);

    // Fabric Network
    this.node = new Peer(this.settings);
    this.node.on('error', (error) => {
      this.emit('error', `Agent error: ${error}`);
    });

    // Local Actor
    this.actor = new Actor(this.settings.identity);

    // Create the Verse
    this.verse = new Verse();
    this.verse.on('commit', this._handleVerseCommit.bind(this));

    this.contract = (this.settings.contract) ? { id: this.settings.contract } : null;
    this.messages = {};

    // Track the core's clock
    this.ticker = null;

    // Fabric State
    this._state = {
      content: this.settings.state
    };

    // Set status
    this.status = 'ready';

    // Return instance explicitly
    return this;
  }

  get clock () {
    return this._state.content.clock || 0;
  }

  set clock (value) {
    this._state.content.clock = value;
  }

  // TODO: debug memory leak here
  commit () {
    this.fs.publish('STATE', JSON.stringify(this.state, null, '  '));

    try {
      const changes = monitor.generate(this.observer);

      if (changes.length) {
        this.emit('changes', changes);

        const PACKET_CONTRACT_MESSAGE = Message.fromVector(['CONTRACT_MESSAGE', {
          type: 'CONTRACT_MESSAGE',
          object: {
            contract: this.contract.id,
            ops: changes
          }
        }]).toBuffer();

        this.node.broadcast(PACKET_CONTRACT_MESSAGE);
      }

    } catch (exception) {
      console.trace(`Unable to get changes: ${exception}`);
    }

    return this;
  }

  /**
   * Deploys the VERSE contract to the Fabric Network.
   * @returns {String} Message ID.
   */
  async deploy () {
    // Attest to local time
    const now = (new Date()).toISOString();
    const input = {
      clock: 0,
      validators: [],
      verse: {}
    };

    // First message (genesis)
    const PACKET_CONTRACT_GENESIS = Message.fromVector(['CONTRACT_GENESIS', JSON.stringify({
      type: 'CONTRACT_GENESIS',
      object: {
        input: input
      }
    })])._setSigner(this.signer).sign().toBuffer();

    let hash = null;

    // Get hash of message
    if (typeof window !== 'undefined' && window.crypto && window.crypto.getRandomValues) {
      const msgUint8 = new TextEncoder().encode(PACKET_CONTRACT_GENESIS.toString('utf8'));
      hash = await crypto.subtle.digest('SHA-256', msgUint8);
    } else {
      hash = crypto.createHash('sha256').update(PACKET_CONTRACT_GENESIS).digest('hex');
    }

    // Store locally
    this.messages[hash] = PACKET_CONTRACT_GENESIS.toString('hex');

    // Contract template
    const template = {
      author: this.node.identity.pubkey,
      bond: null, // BTC transaction which is spent
      checksum: '',
      created: now,
      genesis: hash,
      history: [ hash ], // most recent first
      messages: this.messages,
      name: this.settings.name,
      signature: '',
      state: input,
      version: 1
    };

    // Track our contract by Actor ID
    this.contract = new Actor(template);
    this.emit('log', `Deploying Contract [0x${this.contract.id}] (${PACKET_CONTRACT_GENESIS.byteLength} bytes): ${this.messages[hash]}`);

    // Network publish message (contract)
    const PACKET_CONTRACT_PUBLISH = Message.fromVector(['CONTRACT_PUBLISH', JSON.stringify({
      type: 'CONTRACT_PUBLISH',
      object: template
    })]);

    // Broadcast to all peers
    this.node.broadcast(PACKET_CONTRACT_PUBLISH.toBuffer());

    // Return contract ID
    return this.contract.id;
  }

  propose (ops) {
    // TODO: $BTC psbt
    return {
      ops: ops,
      psbt: null
    };
  }

  async tick () {
    const start = Date.now();
    this.clock++;
    await this.verse.tick();

    Object.assign(this._state.content, this.state, {
      verse: _sortKeys(this.verse.state)
    });

    this.commit();

    this.emit('tick', {
      meta: {
        lastTickTime: Date.now() - start,
        characterCount: Object.keys(this.state.verse.characters).length,
        dbSize: Buffer.from(JSON.stringify(this.state), 'utf8').byteLength,
        pathCount: Object.keys(this.state.verse.paths).length,
        placeCount: Object.keys(this.state.verse.places).length,
        playerCount: Object.keys(this.state.verse.players).length
      },
      state: this.state
    });
  }

  _handleVerseCommit (commit) {
    this._state.content.verse = commit.state;
    // this.commit();
  }

  async start () {
    this.emit('log', 'Starting VERSE…');

    // Load from filesystem
    await this.fs.start();

    const file = this.fs.readFile('STATE');
    const state = (file) ? JSON.parse(file) : this.state;

    // Import local state
    this._state.content = _sortKeys(state);
    this.verse._state.content = state.verse || this.verse.state;

    // Monitor changes
    try {
      this.observer = monitor.observe(this._state.content);
    } catch (exception) {
      console.error(`Could not watch: ${exception}`);
    }

    // Start services
    await this.verse.start();
    await this.http.start();
    await this.node.start();

    // Update state
    this._state.content.status = 'STARTED';

    // Deploy if no contract is known
    if (!this.settings.contract) this.deploy();

    // Commit
    this.commit();

    // Create ticker
    this.ticker = setInterval(() => {
      this.tick();
    }, this.settings.interval);

    return this;
  }
}

// Make a Promise
module.exports = Core;
