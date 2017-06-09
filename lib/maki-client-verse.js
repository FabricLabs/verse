function VersePlugin (config) {
  var self = this;
  var resources = {};
  
  if (config.resource) {
    resources[config.resource] = {
      plugin: function (schema, options) {
        schema.add({ id: String });
        schema.pre('save', function (next) {
          var self = this;
          if (!self.id) self.id = self.slug || self.username;
          next();
        });
      }
    };
  }
  
  self.extends = {
    resources: resources,
    services: {
      http: {
        //client: __dirname + '/client'
        //client: __dirname + '/example'
        client: __dirname + '/verse'
      }
    }
  }
}

module.exports = VersePlugin;