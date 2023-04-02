'use strict';

// Dependencies
const BN = require('bn.js');

// Voxel Types
const types = {
  'void': new BN(0),
  'worldstone': new BN(1),
  'granite': new BN(2),
  'sandstone': new BN(3),
  'gravel': new BN(4),
  'sand': new BN(5),
  'coal': new BN(6),
  'soil': new BN(7),
  'grass': new BN(8)
};

class Voxel {
  constructor (x, y, z, type) {
    if (!(x instanceof BN)) x = new BN(x, 10);
    if (!(y instanceof BN)) y = new BN(y, 10);
    if (!(z instanceof BN)) z = new BN(z, 10);

    this.memory = Buffer.alloc(8 + 8 + 8 + 8); // x, y, z, type
    this.type = (typeof type == 'string' && types[type]) ? types[type] : 1;

    this.x = x || null;
    this.y = y || null;
    this.z = z || null;

    return this;
  }

  set x (value) {
    if (!(value instanceof BN)) value = new BN(value, 10);
    this.memory.write(value.toString(16, 16), 0, 8, 'hex');
  }

  set y (value) {
    if (!(value instanceof BN)) value = new BN(value, 10);
    this.memory.write(value.toString(16, 16), 8, 8, 'hex');
  }

  set z (value) {
    if (!(value instanceof BN)) value = new BN(value, 10);
    this.memory.write(value.toString(16, 16), 16, 8, 'hex');
  }

  set type (value) {
    if (!(value instanceof BN)) value = new BN(value, 10);
    this.memory.write(value.toString(16, 16), 24, 8, 'hex');
  }

  get x () {
    return this.memory.slice(0, 8);
  }

  get y () {
    return this.memory.slice(8, 16);
  }

  get z () {
    return this.memory.slice(16, 24);
  }

  get type () {
    return this.memory.slice(24, 32);
  }

  toBuffer () {
    return this.memory;
  }
}

module.exports = Voxel;
