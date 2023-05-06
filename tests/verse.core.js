'use strict';

const assert = require('assert');
const Core = require('../types/core');

describe('@fabric/verse/types/core', function () {
  describe('Core', function () {
    it('can smoothly create a default core', function () {
      const core = new Core();

      assert.ok(core);
      assert.ok(core.id);
    });

    it('can start and stop', async function () {
      const core = new Core();

      await core.start();
      await core.stop();

      assert.ok(core);
      assert.ok(core.id);
    });
  });
});
