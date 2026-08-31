const WebSocket = require('isomorphic-ws');

class Bridge extends HTMLElement {
  constructor (settings) {
    super(...settings);

    this.settings = Object.assign({
      state: {
        status: 'PAUSED'
      }
    });

    this._state = {
      content: this.settings.state
    };

    return this;
  }

  async start () {
    this.socket = new WebSocket()
  }
}

module.exports = Bridge;
