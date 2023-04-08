 'use strict';

// Fabric HTTP Types
const FabricSite = require('@fabric/http/types/site');

class Site extends FabricSite {
  _getHTML (state) {
    // TODO: obvious modularization...
    // - fabric-site
    //   - fabric-bridge
    //   - fabric-console
    //   - fabric-menu
    //   - fabric-grid
    return `
      <fabric-site class="ui container" id="site">
        <fabric-bridge host="localhost" port="9999" secure="false"></fabric-bridge>
        <fabric-assets>
          <audio id="bgm" loop="true" src="/sounds/irvingforce-crime-scanner.mp3" />
        </fabric-assets>
        <fabric-console id="console" style="display: none;">
          <fabric-card class="ui fluid card">
            <fabric-card-content class="content">
              <p>Console...</p>
            </fabric-card-content>
          </fabric-card>
        </fabric-console>
        <fabric-menu id="tray">
          <i id="tray-settings" class="ui large inverted cog icon"></i>
          <i id="volume" class="ui large inverted volume up icon"></i>
        </fabric-menu>
        <fabric-menu>
          <fabric-card id="settings" class="ui fluid card" style="display: none;">
            <fabric-card-header class="ui header">Settings</fabric-card-header>
            <fabric-card-content class="ui content">
              <p>Foo</p>
            </fabric-card-content>
          </fabric-card>
        </fabric-menu>
        <fabric-grid class="ui centered grid">
          <fabric-column class="twelve wide column">
            <fabric-card class="ui fluid card" id="overlay">
              <fabric-card-content class="content" style="text-align: center;">
                <h1 class="ui huge header" data-bind="/title"><code>${state.title || this.title || this.state.title || 'V E R S E' || 'Example Application'}</code></h1>
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
        <fabric-footer>
          <fabric-card class="ui fluid card" id="dialogue" style="display: none;">
            <fabric-card-content class="ui content">
              <p style="display: inline-block;"><em>Dialogue...</em></p>
            </fabric-card-content>
          </fabric-card>
          <fabric-card class="ui fluid card" id="footer">
            <fabric-card-content class="ui content">
              <i id="chat-close" class="large inverted close icon" style="float: right; display: none;"></i>
              <i class="inverted console icon" style="float: right;"></i>
              <fabric-chat-view>
                <p><strong><abbr title="Message Of The Day">MOTD</abbr>:</strong> YOLO!</p>
              </fabric-chat-view>
              <form class="ui inverted form">
                <input name="input" type="text" placeholder="..." class="ui transparent input" style="display: none;" />
              </form>
            </fabric-card-content>
          </fabric-card>
        </fabric-footer>
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
              <p style="display: inline-block;"><em>Dialogue...</em></p>
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
