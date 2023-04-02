'use strict';

const Verse = require('../types/verse');


async function main (input = {}) {
  fetch('cb55a346d20d4c37babb.module.wasm')
    .then((response) => response.arrayBuffer())
    .then((bytes) => WebAssembly.instantiate(bytes, importObject))
    .then(async (results) => {
      console.log('wasm results:', results);
      await engine.start();

      console.log('started:', engine);
    });
  const engine = new Verse(input);

  return {
    engine: engine.id
  };
}

main().catch((exception) => {
  console.log('[VERSE] Error:', exception);
}).then((output) => {
  console.log('[VERSE] Process Started:', output);
});
