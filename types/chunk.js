'use strict';

const Voxel = require('./voxel');

class Chunk {
  constructor (size = 32) {
    this.size = size;

    this.matrix = [];
    this.voxels = [];

    for (let x = 0; x < this.size; x++) {
      this.matrix[x] = new Array(this.size);
      for (let y = 0; y < this.size; y++) {
        this.matrix[x][y] = new Array(this.size);
        for (let z = 0; z < this.size; z++) {
          this.matrix[x][y][z] = new Voxel(x, y, z);
        }
      }
    }

    return this;
  }

  get coordinates () {
    const coordinates = [];

    for (let x = 0; x < this.size; x++) {
      for (let y = 0; y < this.size; y++) {
        for (let z = 0; z < this.size; z++) {
          coordinates.push([x, y, z]);
        }
      }
    }

    return coordinates;
  }

  get length () {
    return Math.pow(this.size, 3);
  }

  each (method) {
    const results = [];

    for (let x = 0; x < this.size; x++) {
      for (let y = 0; y < this.size; y++) {
        for (let z = 0; z < this.size; z++) {
          results.push(method.call(this, this.matrix[x][y][z]));
        }
      }
    }

    return results;
  }

  getVoxel (x, y, z) {
    return this.matrix[x][y][z];
  }

  toBuffer () {
    const list = this.each((voxel) => {
      return voxel.toBuffer();
    });

    return Buffer.concat(list);
  }
}

module.exports = Chunk;
