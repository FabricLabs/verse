'use strict';

// Dependencies
const assert = require('assert');
const Sandbox = require('@fabric/http/types/sandbox');
const Engine = require('../types/core');

describe('VERSE', function () {
  before(async () => {
    this.verse = new Engine({ http: { port: 8484 } });
    await this.verse.start();
  });

  after(async () => {
    await this.verse.stop();
  });

  describe('Web App', function () {
    this.timeout(30000);

    it('can navigate to the server', async function () {
      const sandbox = new Sandbox();
      await sandbox.start();
      await sandbox._navigateTo('http://localhost:8484/');
      await sandbox.stop();
      assert.ok(sandbox);
    });
  });
});
