'use strict';

// Test Dependency
const assert = require('assert');

// Schema-specific Dependencies
const validator = require('is-my-json-valid');

// Types
const Place = require('../types/place');

// Schema Validation
const schemata = require('../schemata');
const validate = validator(schemata.Place);

describe('JSON Schema Compliance', function () {
  describe('@verse/core/types/place', function () {
    it('produces valid JSON', function () {
      const actor = new Place({
        _id: 1,
        name: 'Satoshi Nakamoto'
      });

      try {
        const valid = validate(actor.state);
        assert.ok(valid);
      } catch (exception) {
        console.error('invalid:', exception);
      }

      assert.ok(actor);
      assert.ok(actor.id);
    });
  });
});
