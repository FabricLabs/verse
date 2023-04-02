'use strict';

const BN = require('bn.js');

const Chunk = require('../types/chunk');
const Voxel = require('../types/voxel');

async function main () {
  const size = 2;
  const chunk = new Chunk(size);

  console.log('chunk:', chunk);
  console.log('matrix:', chunk.matrix);
  console.log('memory size:', Math.pow(size, 3) * (8 + 8 + 8 + 8), 'bytes');
  console.log('voxel count:', chunk.coordinates.length);
  console.log('coordinates:', chunk.coordinates);
  console.log('buffer:', chunk.toBuffer());

  return {
    chunks: [chunk]
  };
}

main().catch((exception) => {
  console.error('error:', exception);
}).then((output) => {
  console.log('output:', output);
});
