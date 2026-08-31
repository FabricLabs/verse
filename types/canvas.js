const { createCanvas, loadImage } = require('canvas');

class Canvas {
  constructor (settings = {}) {
    this.id = null;
    this.height = settings.height || 480;
    this.width = settings.width || 640;
    this.canvas = createCanvas(this.width, this.height);
    return this;
  }

  connectedCallback () {
    this.canvas.width = this.width;
    this.canvas.height = this.height;

    const ctx = this.canvas.getContext('2d');
    loadImage('https://fabric.pub/assets/fabric-labs.png').then((image) => {
      ctx.drawImage(image, 0, 0);
    });
  }

  loadImageURL (url) {

  }

  toHTML () {
    return `<canvas id="${this.id}" height="${this.height}" width="${this.width}"></canvas>`;
  }
}

module.exports = Canvas;
