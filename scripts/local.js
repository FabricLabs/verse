'use strict';

const BN = require('bn.js');

const Chunk = require('../types/chunk');
const Sheet = require('../types/sheet');
const Voxel = require('../types/voxel');

async function main () {
  const size = 128;
  const chunk = new Chunk(size);
  const sheet = new Sheet({ size });

  const data = await sheet.loadFromURL('https://fabric.pub/assets/fabric-labs.png');
  const sprite = sheet.getChunk();

  // Disabled expensive logs
  console.log('chunk:', chunk);
  // console.log('matrix:', chunk.matrix);
  console.log('memory size:', Math.pow(size, 3) * (8 + 8 + 8 + 8), 'bytes');
  // console.log('voxel count:', chunk.coordinates.length);
  // console.log('coordinates:', chunk.coordinates);
  console.log('buffer:', chunk.toBuffer());

  console.log('data:', data);
  console.log('chunkCount:', sheet.loadableSheetCount);
  console.log('sprite:', sprite);

  return {
    chunks: [chunk]
  };
}

main().catch((exception) => {
  console.error('error:', exception);
}).then((output) => {
  console.log('output:', output);
});
