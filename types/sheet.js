'use strict';

const { createCanvas, loadImage } = require('canvas');

class Sheet {
  constructor (settings = {}) {
    this.settings = {
      size: settings.size || 32
    };

    this.height = this.settings.size;
    this.width = this.settings.size;

    this.canvas = createCanvas(this.width, this.height);
    this.image = null;

    return this;
  }

  async loadFromURL (url) {
    const ctx = this.canvas.getContext('2d');
    return new Promise((resolve, reject) => {
      loadImage(url).error(reject).then((image) => {
        this.image = image;
        this.height = image.height;
        this.width = image.width;
        ctx.drawImage(image, 0, 0);
        resolve(this);
      });
    });
  }

  get loadableSheetCount () {
    return Math.floor(this.image.width / this.settings.size);
  }

  getChunk (index = 0) {
    const ctx = this.canvas.getContext('2d');

    let row = 0;
    let sx = index * this.settings.size;
    let sy = row * this.settings.size;

    return ctx.getImageData(sx, sy, this.settings.size, this.settings.size);
  }
}

module.exports = Sheet;
