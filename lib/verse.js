'use strict';

// first, load a dependency… ;)
const Fabric = require('@fabric/core');

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
class Verse extends Fabric {
  /**
   * Creates an instance of {@link Verse}, usually with a {@link Configuration}.
   * @param  {Configuration} config Extra configuration beyond the defaults.
   * @return {Verse}        Resulting instance of the {@link Verse}.
   */
  constructor (config) {
    // start the core
    super(config);

    // now that we're running, we can assign some state
    this.seed = Fabric.random(); // UInt64
    this.status = 'seeded'; // Enum

    // we're done.  checkout!
    return this;
  }

  // design your user interface here
  static get render () {
    return (
      <element thanks='{this.key}'>
        <Log type="TODO" />
        <Request />
        <Response />
        <Jobs />
        <Progress />
        <Stats />
      </element>
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
