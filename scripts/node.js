'use strict';
/**
 * Generic Node process for VERSE.
 */

// VERSE Dependencies
const Core = require('../types/core');

// Settings
const settings = require('../settings/local');

// Main Process
async function main (input = null) {
  // VERSE Core
  const core = new Core(input);

  // Handle core errors
  core.on('error', (msg) => {
    console.error('[VERSE] Error:', msg);
  });

  // Report core debug messages
  core.on('debug', (msg) => {
    console.debug('[VERSE] Debug:', msg);
  });

  // Report core log messages
  core.on('log', (msg) => {
    console.log('[VERSE] Log:', msg);
  });

  // Report core ticks
  core.on('tick', (msg) => {
    console.debug('[VERSE] Tick:', msg);
  });

  // Start core
  await core.start();

  // Return report ID
  return {
    id: core.id
  };
}

// Execution
main(settings).catch((exception) => {
  console.error('[VERSE] Main Process Exception:', exception);
}).then((output) => {
  console.log('[VERSE] Main Process Output:', output);
});
