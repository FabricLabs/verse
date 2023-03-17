'use strict';

const Core = require('../types/core');
const settings = require('../settings/local');

async function main (input = null) {
  const core = new Core(input);

  core.on('error', (msg) => {
    console.error('[VERSE] Error:', msg);
  });

  core.on('debug', (msg) => {
    console.debug('[VERSE] Debug:', msg);
  });

  core.on('log', (msg) => {
    console.log('[VERSE] Log:', msg);
  });

  core.on('tick', (msg) => {
    console.debug('[VERSE] Tick:', msg);
  });

  await core.start();

  return {
    id: core.id
  };
}

main(settings).catch((exception) => {
  console.error('[VERSE] Main Process Exception:', exception);
}).then((output) => {
  console.log('[VERSE] Main Process Output:', output);
});
