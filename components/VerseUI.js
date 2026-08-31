'use strict';

const worlds = {
  fantasy: {
    high: {
      deity: ['Gaia'],
      noble: ['Gaia'],
      wild: ['Gaia']
    },
    low: {
      clerk: ['Town Square'],
      worker: ['The Mines']
    }
  },
  futuristic: {
    dystopian: {
      survivor: ['The Wastelands'],
      predator: ['The Maw']
    },
    technocratic: {
      soldier: ['Gambit\'s Bar'],
      pilot: ['Wing City Spaceport'],
      worker: ['Main Street']
    }
  },
  realistic: {
    contemporary: {
      human: ['Gambit\'s Bar']
    }
  }
};

const PROMPT = 'VERSE is a veritable multiverse, where worlds collide and legends are made.\n\nTo better select a starting location, choose a genre:';
