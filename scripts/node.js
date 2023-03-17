'use strict';

const Core = require('../types/core');

async function main () {
  const core = new Core();

  core.on('debug', (msg) => {
    console.debug('[VERSE] Debug:', msg);
  });
  core.on('log', (msg) => {
    console.log('[VERSE] Log:', msg);
  });

  await core.start();

  return {
    id: core.id
  };
}

main().catch((exception) => {
  console.error('[VERSE] Main Process Exception:', exception);
}).then((output) => {
  console.log('[VERSE] Main Process Output:', output);
});
