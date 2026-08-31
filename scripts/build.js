'use strict';

const path = require('path');

// Settings
const settings = require('../settings/local');

// Fabric HTTP Types
// const Site = require('@fabric/http/types/site');
const Compiler = require('@fabric/http/types/compiler');

// Plugins
const BundleAnalyzerPlugin = require('webpack-bundle-analyzer').BundleAnalyzerPlugin;

// Types
const Place = require('../types/place');
const Site = require('../types/site');

// Program Body
async function main (input = {}) {
  const site = new Site(input);
  const compiler = new Compiler({
    document: site,
    state: {
      title: 'V E R S E'
    },
    types: {
      Place
    },
    webpack: {
      // mode: 'production',
      plugins: [
        // new BundleAnalyzerPlugin()
      ]
      // entry: path.resolve('./scripts/navigator.js'),
      // target: 'web', 
      /* output: {
        path: path.resolve('./assets/bundles'),
        filename: 'navigator.js'
      },
      devtool: 'inline-source-map',
      module: {
        rules: [
          {
            test: /\.(js)$/,
            use: ['babel-loader']
          },
          {
            test: /\.css$/,
            use: [
              {
                loader: 'style-loader'
              },
              {
                loader: 'css-loader',
                options: {
                  modules: true,
                  sourceMap: true
                }
              }
            ]
          }
        ]
      } */
    }
  });

  await compiler.compileTo('assets/index.html');

  return {
    site: site.id
  };
}

// Run Program
main(settings).catch((exception) => {
  console.error('[VERSE:BUILD]', '[EXCEPTION]', exception);
}).then((output) => {
  console.log('[VERSE:BUILD]', '[OUTPUT]', output);
});
