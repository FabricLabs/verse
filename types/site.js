 'use strict';

// Fabric HTTP Types
const FabricSite = require('@fabric/http/types/site');

class Site extends FabricSite {
  constructor (settings = {}) {
    // Adopt Fabric semantics
    super(settings);

    // Define local settings
    this.settings = Object.assign({
      authority: 'http://localhost:9332/services/fabric', // loopback service
      fabric: {
        name: '@sites/default'
      },
      state: {
        title: 'Default Site'
      },
      spa: null
    }, this.settings, settings);

    // Set local state
    this._state = {
      content: this.settings.state,
      status: 'PAUSED'
    };

    // Fabric Components
    // this.bridge = new Bridge();

    // Ensure chainability
    return this;
  }

  _getHTML (state) {
    // TODO: obvious modularization...
    // - fabric-site
    //   - fabric-bridge
    //   - fabric-console
    //   - fabric-menu
    //   - fabric-grid
    return `
      <fabric-site class="ui fluid container" id="site">
        <fabric-bridge host="localhost" port="9999" secure="false"></fabric-bridge>
        <fabric-assets>
          <audio id="bgm" loop="true" src="/sounds/irvingforce-crime-scanner.mp3" />
        </fabric-assets>
        <fabric-card class="ui fluid card" id="identity-manager">
          <fabric-card-content class="content">
            <form id="rpg-login-form" class="ui inverted inline form" method="POST">
              <fabric-fields class="ui fields">
                <fabric-form-field class="ui field">
                  <input type="text" name="username" class="input" placeholder="Your RPG username" />
                  <label for="username">Username</label>
                </fabric-form-field>
                <fabric-form-field class="ui field">
                  <input type="password" name="password" placeholder="Your RPG password" autocomplete="current-password" />
                  <label for="password">Password</label>
                </fabric-form-field>
                <fabric-form-field class="ui field">
                  <input class="ui submit button" type="submit" value="Log In" />
                </fabric-form-field>
              </fabric-fields>
            </form>
          </fabric-card-content>
        </fabric-card>
        <fabric-console id="console" style="display: none;">
          <fabric-card class="ui fluid card">
            <fabric-card-content class="ui content">
              <i class="remove icon" id="chat-close" style="float: right;"></i>
              <h3 class="header">Chat</h3>
              <fabric-chat-view class="ui inverted segment">
                <fabric-chat-log>
                  <p><strong><abbr title="Message Of The Day">MOTD</abbr>:</strong> YOLO!</p>
                </fabric-chat-log>
              </fabric-chat-view>
              <form id="chat-input" autocomplete="off" method="POST" class="ui inverted form">
                <fabric-input class="ui labeled fluid input">
                  <fabric-label class="ui label">Say:</fabric-label>
                  <input name="input" type="text" placeholder="..." />
                </fabric-input>
              </form>
            </fabric-card-content>
          </fabric-card>
        </fabric-console>
        <fabric-menu id="tray">
          <i id="tray-settings" class="ui large inverted cog icon"></i>
          <i id="volume" class="ui large inverted volume up icon"></i>
        </fabric-menu>
        <fabric-menu>
          <fabric-card id="settings" class="ui fluid card" style="display: none;">
            <fabric-card-content class="extra content">
              <i class="right floated inverted remove icon" id="settings-close"></i>
            </fabric-card-content>
            <fabric-card-content class="content">
              <h2>Settings</h2>
              <p>Not yet implemented...</p>
            </fabric-card-content>
          </fabric-card>
        </fabric-menu>
        <fabric-grid class="ui centered grid">
          <fabric-column class="twelve wide column">
            <fabric-card class="ui fluid card" id="overlay">
              <fabric-card-content class="content" style="text-align: center;">
                <h1 class="ui huge header" data-bind="/title"><code>V E R S E</code></h1>
                <p>world explorer</p>
              </fabric-card-content>
              <fabric-card-content class="extra">
                <fabric-button-group class="ui large fluid vertical labeled icon buttons">
                  <fabric-button id="connect" class="ui primary button">
                    <i class="ui bolt icon"></i> Connect
                  </fabric-button>
                  <fabric-button class="ui disabled secondary button">
                    <i class="ui tree icon"></i> Wilderness Mode
                  </fabric-button>
                  <fabric-button class="ui disabled secondary button">
                    <i class="ui terminal icon"></i> Text Client
                  </fabric-button>
                  <fabric-button class="ui disabled secondary button">
                    <i class="ui users icon"></i> Manage Peers
                  </fabric-button>
                  <fabric-button class="ui disabled secondary button">
                    <i class="ui bitcoin icon"></i> Wallet
                  </fabric-button>
                  <fabric-button class="ui disabled secondary button">
                    <i class="ui save icon"></i> Load...
                  </fabric-button>
                </fabric-button-group>
              </fabric-card-content>
              <fabric-card-content class="extra hidden" style="display: none;">
                <h2>Navigation</h2>
                <table class="ui center aligned table">
                  <tr>
                    <td><fabric-button class="ui button">northwest</fabric-button></td>
                    <td><fabric-button class="ui button">north</fabric-button></td>
                    <td><fabric-button class="ui button">northeast</fabric-button></td>
                  </tr>
                  <tr>
                    <td><fabric-button class="ui button">west</fabric-button></td>
                    <td><fabric-button class="ui button">(you)</fabric-button></td>
                    <td><fabric-button class="ui button">east</fabric-button></td>
                  </tr>
                  <tr>
                    <td><fabric-button class="ui button">southwest</fabric-button></td>
                    <td><fabric-button class="ui button">south</fabric-button></td>
                    <td><fabric-button class="ui button">southeast</fabric-button></td>
                  </tr>
                </table>
              </fabric-card-content>
              <fabric-card-content class="extra hidden" style="display: none;">
                <h2>Debug</h2>
              </fabric-card-content>
              <fabric-card-content class="bottom attached" style="display: none;">
                <fabric-button-group class="ui small bottom attached left aligned buttons">
                  <fabric-button class="ui labeled icon button"><i class="ui linkify icon"></i> <code>${this.id}</code></fabric-button>
                </fabric-button-group>
              </fabric-card-content>
            </fabric-card>
          </fabric-column>
        </fabric-grid>
        <verse-dialogue>
          <verse-dialogue-stack></verse-dialogue-stack>
        </verse-dialogue>
        <fabric-dialog-modal>
          <fabric-card class="ui fluid card" id="modal" style="display: none;">
            <fabric-card-content class="ui content">
              <p style="display: inline-block;">Connecting...</p>
            </fabric-card-content>
          </fabric-card>
        </fabric-dialog-modal>
        <template id="dialogue-template">
          <fabric-card class="ui fluid dialogue card" style="display: none;">
            <fabric-card-content class="ui content">
              <div class="typed-out">&hellip;</div>
            </fabric-card-content>
            <fabric-card-content class="extra content">
              <fabric-buttons class="ui one right floated buttons">
                <fabric-button class="ui basic inverted icon right labeled dismiss button">Next <i class="right chevron icon"></i></fabric-button>
              </fabric-buttons>
            </fabric-card-content>
          </fabric-card>
        </template>
      </fabric-site>
      <fabric-script>
        <script>
          class FabricHTMLComponent extends HTMLElement {
            constructor (settings) {
              super(settings);
            }
          }
          customElements.define('fabric-site', FabricHTMLComponent);
        </script>
      </fabric-script>
    `.trim();
  }
}

module.exports = Site;
