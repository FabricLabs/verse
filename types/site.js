 'use strict';

const Peer = require('@fabric/core/types/peer');

// Fabric HTTP Types
const FabricSite = require('@fabric/http/types/site');
const SPA = require('@fabric/http/types/spa');

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
    this.peer = new Peer(this.settings.fabric);
    this.spa = new SPA(this.settings);
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
        <fabric-card class="ui fluid card" id="identity-manager" style="display: none;">
          <fabric-card-content class="content" />
        </fabric-card>
        <verse-character-selection id="character-selection" class="ui fluid card" style="display: none;">
          <fabric-card-content class="content">
            <h3>Character Selection</h3>
          </fabric-card-content>
          <fabric-card-content class="content">
            <button id="start-new-story" class="ui huge green fluid right labeled icon button"><span><i class="leaf icon"></i> New Story</span><i class="right chevron icon"></i></button>
          </fabric-card-content>
          <fabric-card-content class="content">
            <fabric-input class="ui fluid input">
              <input type="text" name="characterQuery" placeholder="Filter..." />
            </fabric-input>
          </fabric-card-content>
          <fabric-card-content class="content" style="overflow: scroll;">
            <verse-character-list class="ui cards"></verse-character-list>
          </fabric-card-content>
        </verse-character-selection>
        <verse-character-creator style="display: none;" id="character-creator">
          <fabric-card class="ui fluid card">
            <fabric-card-content class="content">
              <h3>Create a New Character</h3>
              <form id="creator-form" class="ui large form" method="POST">
                <fabric-field class="ui field">
                  <label>Name</label>
                  <input type="text" name="name" class="ui required input" placeholder="(your character's name)" required="true" />
                </fabric-field>
                <fabric-field class="ui field">
                  <label>Synopsis</label>
                  <textarea name="synopsis" class="ui required input" placeholder="(a short description of your character)" required="true"></textarea>
                </fabric-field>
                <fabric-field class="ui disabled field" style="display: none;">
                  <label>Background</label>
                  <div class="ui selection dropdown">
                    <input type="hidden" name="background" value="0">
                    <i class="dropdown icon"></i>
                    <div class="default text">Background</div>
                    <div class="menu">
                      <div class="default item" data-value="0">Generic</div>
                    </div>
                  </div>
                </fabric-field>
                <fabric-field class="ui disabled field" style="display: none;">
                  <label>Spawn Location</label>
                  <div class="ui selection dropdown">
                    <input type="hidden" name="spawn" value="175">
                    <i class="dropdown icon"></i>
                    <div class="default text">Spawn Location</div>
                    <div class="menu">
                      <div class="default item" data-value="175">Wing City Spaceport</div>
                    </div>
                  </div>
                </fabric-field>
                <fabric-field class="ui field">
                  <button class="ui large fluid right labeled icon button" style="margin-top: 1em;">Generate Seed <i class="right chevron icon"></i></button>
                </fabric-field>
              </form>
            </fabric-card-content>
          </fabric-card>
        </verse-character-creator>
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
                <fabric-container class="ui fluid text container">
                  <form id="rpg-login-form" class="ui inverted form" method="POST">
                    <fabric-form-field class="ui field">
                      <label for="username">Username</label>
                      <input type="text" name="username" class="required input" required="true" placeholder="Your RPG username" autocomplete="username" />
                    </fabric-form-field>
                    <fabric-form-field class="ui field">
                      <label for="password">Password</label>
                      <input type="password" name="password" class="required input" required="true" placeholder="Your RPG password" autocomplete="current-password" />
                    </fabric-form-field>
                    <button class="ui fluid primary right labeled icon button" type="submit" style="margin-top: 1em;">
                      <i class="right chevron icon"></i>
                      Authenticate
                    </button>
                  </form>
                </fabric-container>
              </fabric-card-content>
              <fabric-card-content class="extra content desktop-only">
                <fabric-button-group class="ui large fluid vertical labeled icon buttons">
                  <fabric-button class="ui disabled secondary button">
                    <i class="ui tree icon"></i> Wilderness Mode
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
        <verse-dialogue-stack class="ui stacked cards"></verse-dialogue-stack>
        <fabric-chat-bar id="chat-bar">
          <fabric-card class="ui fluid card">
            <fabric-card-content id="chat-log" class="extra content" style="display: none;">
              <fabric-chat-log style="height: 30em;">
                <fabric-chat-entry>
                  <p><strong><abbr title="Message Of The Day">MOTD</abbr>:</strong> YOLO!</p>
                </fabric-chat-entry>
              </fabric-chat-log>
            </fabric-card-content>
            <fabric-card-content class="content">
              <form class="ui form" id="chat-input">
                <fabric-form-field class="ui field">
                  <fabric-input class="ui transparent inverted left action fluid input">
                    <fabric-button class="ui inverted basic button">Say:</fabric-button>
                    <input type="text" name="input" placeholder="..." autocomplete="off" />
                    <fabric-button class="icon button" id="chat-collapse" style="display: none;"><i class="down chevron icon"></i></fabric-button>
                  </fabric-input>
                </fabric-form-field>
              </form>
            </fabric-card-content>
          </fabric-card>
        </fabric-chat-bar>
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
