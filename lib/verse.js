'use strict';

var events = require('events');
var util = require('util');

var Engine = require('voxel-engine-stackgl');

function Verse (config) {
  self.config = config;
}

util.inherits(Verse, events.EventEmitter);

Verse.prototype.start = function () {
  var self = this;
  
  self.engine = require('voxel-engine-stackgl')({
    container: container,
    pluginLoaders: {
      'voxel-engine-stackgl': require('voxel-engine-stackgl'),
      'voxel-registry': require('voxel-registry'),
      //'voxel-bedrock': require('voxel-bedrock'),
      //'voxel-flatland': require('voxel-flatland')
    },
    pluginOpts: {
      'voxel-engine-stackgl': { generateChunks: true },
      'game-shell-fps-camera': {position: [0, -100, 0]},

      //'voxel-bedrock': {},
      //'voxel-flatland': {block: 'bedrock'}
    }
  });
};

window.Verse = Verse;
module.exports = Verse;