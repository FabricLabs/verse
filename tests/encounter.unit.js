'use strict';

const assert = require('assert');
const maybeEncounter = require('../functions/maybeEncounter');

describe('maybeEncounter()', function () {
  describe('Function', function () {
    it('should expose a constructor', function () {
      assert.equal(typeof maybeEncounter, 'function');
    });

    it('can generate an encounter with 100% probability', async function () {
      const encounter = await maybeEncounter({ probabilities: [1], types: ['battle'] });
      assert.ok(encounter);
    });
  });
});
