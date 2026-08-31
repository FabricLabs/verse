'use strict';

class Octree {
  constructor (settings) {
    super(settings);
    this.settings = Object.assign({}, settings);
    this.children = [];
    this._state = { content: {} };
    return this;
  }
}

module.exports = Octree;
