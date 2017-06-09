var Maki = require('maki');

// TODO: use app.configure
var app = new Maki({
  service: {
    name: 'verse',
    icon: 'cube',
    pitch: 'create and explore imaginary worlds',
    about: 'This is an experimental component of <a href="https://maki.io">a much larger vision</a>, emerging from <a href="https://www.roleplaygateway.com">a decades-old community</a> to explore what is <strong>possible</strong>.',
    contract: '',
    copyright: 'Copyright can be a contentious topic.  We\'d like to start that conversation.  Read <a href="#">our statement on copyright</a>.',
    links: {
      github: 'https://github.com/martindale/verse'
    }
  },
  services: {
    http: {
      port: 7766
    }
  }
});

var Verse = require('./lib/maki-client-verse');
var verse = new Verse({
  resource: 'Asset'
});

app.use(verse);

app.define('Index', {
  public: false,
  name: 'Index',
  templates: {
    query: 'splash'
  },
  components: {
    query: 'verse-ui'
  },
  routes: {
    query: '/'
  },
  static: true
});

var Script = app.define('Script', {
  icon: 'code',
  //source: './scripts',
  attributes: {
    name: { type: String , id: true },
    hash: { type: String },
    content: { type: String },
  }
});

var Asset = app.define('Asset', {
  icon: 'cubes',
  attributes: {
    data: { type: Object },
    coordinates: {
      name: { type: String },
      hash: { type: String },
      content: { type: String },
    }
  }
});

var Instance = app.define('Instance', {
  icon: 'star alt',
  attributes: {
    data: { type: Object },
    coordinates: {
      x: { type: Number , default: 0 },
      y: { type: Number , default: 0 },
      z: { type: Number , default: 0 }
    }
  }
});

app.start();
