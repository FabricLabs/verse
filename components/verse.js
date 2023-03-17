'use strict';

import React, { Component } from 'react';
import { Grid } from 'semantic-ui-react';

// first, load a dependency… ;)
const Fabric = require('@fabric/core');
const App = require('./app');

/**
 * Fabric uses the {@link NativeComponent} type to render output.  Explicitly
 * setting properties
 * @property {Seed} seed Entropy for the component.
 * @property {Enum} status {@link Machine} status.
 * @property {Fabric} fabric Local instance of {@link Fabric}.
 */
class NativeComponent extends Component {
  /**
   * User interface element for {@link Fabric}.
   * @param  {Configuration} config Extra configuration beyond the defaults.
   * @return {Verse}        Resulting instance of the {@link Verse}.
   */
  constructor (config) {
    // start the core
    super(config);

    // now that we're running, we can assign some state
    this.seed = Fabric.random(); // UInt64
    this.status = 'seeded'; // Enum

    // create a background service with Fabric
    this.fabric = new Fabric(config);

    this.fabric.once('ready', this.ready.bind(this));
    this.fabric.on('patches', this._handlePatches.bind(this));

    // we're done.  checkout!
    return this;
  }

  async ready (event) {
    super.ready();
    return this.render();
  }
}

/**
 * Verse is an economic engine built with Fabric, a protocol for distributed
 * applications.  It's meant to be easily understood, so all source code is
 * reasonably-well commented.  If you're already an experienced engineer, you
 * might even find it too verbose — so parameters are also easily intuited.
 *
 * When using `git`, typically you can use the `npm` package manager to start
 * your program with: `npm start` or `npm run docs` to review the documentation
 * locally (recommended).
 * @property {Seed} seed Initial seed.  Immutable.
 * @property {Enum} status {@link Machine} status.
 * @property {State} state Current (certified) state snapshot.
 */
export default class Verse extends NativeComponent {
  /**
   * Creates an instance of {@link Verse}, usually with a {@link Configuration}.
   * @param  {Configuration} config Extra configuration beyond the defaults.
   * @return {Verse}        Resulting instance of the {@link Verse}.
   */
  constructor (config) {
    // start the core
    super(config);

    // now that we're running, we can assign some state
    this.status = 'ready';

    // we're done.  checkout!
    return this;
  }

  // design your user interface here
  render () {
    return (
      <div thanks='{this.key}'>
        <Grid className="fullscreen">
          <Log type="TODO" />
          <Request />
          <Response />
          <Jobs />
          <Progress />
          <Stats />
        </Grid>
      </div>
    );
  }

  /**
   * Start the Engine.
   * @return {Promise} Resolves on contract end.
   */
  async start () {
    this.log('Starting…');
    return super.start();
  }

  /**
   * Stop the Engine.
   * @return {Promise} Resolves on contract end.
   */
  async stop () {
    this.log('Stopping…');
    return super.stop();
  }

  // function is nulled.
  static set render (value) {
    return null; // throw null pointer later
  }
}

// offer our definition!
module.exports = Verse;
