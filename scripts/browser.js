'use strict';

const {
  SIZE,
  STARTING_POSITION
} = require('../constants');

const THREE = require('three');
// const dat = require('dat.gui');

const { OrbitControls } = require('three/examples/jsm/controls/OrbitControls');
const { ImprovedNoise } = require('three/examples/jsm/math/ImprovedNoise');

const Player = require('../types/player');
const Universe = require('../types/universe');
const Sheet = require('../types/sheet');
const Place = require('../types/place');

const maybeEncounter = require('../functions/maybeEncounter');

async function _loadWASM () {
  /* fetch('cb55a346d20d4c37babb.module.wasm')
    .then((response) => response.arrayBuffer())
    .then((bytes) => WebAssembly.instantiate(bytes, importObject))
    .then(async (results) => {
      console.log('wasm results:', results);
      await engine.start();

      console.log('started:', engine);
    }); */
}

async function main (input) {
  const site = document.getElementById('site');
  const universe = new Universe();

  let isChatting = false;

  window.addEventListener('load', async () => {
    console.log('loaded!');

    await universe.start();
    console.log('universe:', universe);

    // Three.js elements
    const clock = new THREE.Clock();
    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
    const renderer = new THREE.WebGLRenderer({
      alpha: true,
      antialias: true,
      precision: 'highp'
    });

    const mouse = new THREE.Vector2();

    // Rover
    const vehicleGeometry = new THREE.SphereGeometry(0.5, 32, 32);
    const vehicleMaterial = new THREE.MeshBasicMaterial({ color: 0x2185d0 });
    const vehicleGlow = new THREE.PointLight(0x5555ee, 1, 50);

    // Fireballs
    const fireballGeometry = new THREE.SphereGeometry(0.05, 8, 8);
    const fireballMaterial = new THREE.MeshBasicMaterial({ color: 0xFF0000 });

    // Ghost
    const ghostMaterial = new THREE.MeshBasicMaterial({
      color: 0x2185d0,
      transparent: true,
      opacity: 0.2
    });

    // Lighting
    const ambient = new THREE.AmbientLight(0x404040);
    const spotlight = new THREE.SpotLight(0xffffff);
    const lightHelper = new THREE.SpotLightHelper(spotlight);

    // Voxels
    const selectionGlow = new THREE.PointLight(0xffffff, 1, 50);
    const materials = {
      granite: new THREE.MeshBasicMaterial({ color: 0x808080 }),
      sandstone: new THREE.MeshBasicMaterial({ color: 0xC2B280 }),
      soil: new THREE.MeshBasicMaterial({ color: 0x654321 }),
      sand: new THREE.MeshBasicMaterial({ color: 0xF4A460 }),
      air: null
    };

    /**
     * Nomenclature for "Vehicle" here is to reference the player's avatar and influence bubble.
     * @alias {@link Rover}
     */
    const vehicle = createVehicleMesh();
    const ghost = createGhostMesh();

    scene.add(vehicle);
    // scene.add(ghost);

    // Component state
    const animations = [];
    const state = [];

    let cameraModes = ['third-person', 'isometric', 'overhead', 'first-person'];
    let currentCameraMode = 'loading';

    function animate () {
      const time = clock.getElapsedTime();

      requestAnimationFrame(animate);
      resizeCanvasToDisplaySize();

      switch (currentCameraMode) {
        default:
        case 'third-person':
          camera.position.set(vehicle.position.x + 5.2, vehicle.position.y + 5.2, vehicle.position.z);
          camera.lookAt(vehicle.position.x, Math.floor(vehicle.position.y), vehicle.position.z);
          break;
        case 'isometric':
          camera.position.set(vehicle.position.x + 5.2, vehicle.position.y + 5.2, vehicle.position.z + 5.2);
          camera.lookAt(vehicle.position.x, Math.floor(vehicle.position.y), vehicle.position.z);
          break;
        case 'overhead':
          camera.position.set(vehicle.position.x, vehicle.position.y + 8, vehicle.position.z);
          camera.lookAt(vehicle.position.x, 1, vehicle.position.z);
          break;
        case 'first-person':
          // TODO: FIXME
          camera.position.set(vehicle.position.x, vehicle.position.y, vehicle.position.z);
          camera.lookAt(new THREE.Vector3(0, 1.5, 0));
          break;
      }

      vehicle.position.set(vehicle.position.x, (Math.sin(time * 3) * 0.0008) + vehicle.position.y, vehicle.position.z);
      spotlight.position.set(vehicle.position.x, vehicle.position.y + 5, vehicle.position.z);

      animations.forEach(mesh => {
        if (mesh.userData.clock && mesh.userData.mixer) {
          mesh.userData.mixer.update(mesh.userData.clock.getDelta());
        }
      });

      raycaster.setFromCamera(mouse, camera);
      renderer.render(scene, camera);
    }

    const pixelMap = {}
    const voxelMap = {}

    function createPlaneMesh () {
      const group = new THREE.Group();

      for (let i = 0; i < SIZE; i++) {
        for (let j = 0; j < SIZE; j++) {
          const p = createVoxelMesh();

          voxelMap[`${i}:${j}`] = p;

          p.position.set(i, 0, j);
          p.userData.context = 'baseplane';
          p.userData.position = { x: i, y: j };

          group.add(p);
        }
      }

      return group;
    }

    function createMoveAnimation (mesh, startPosition, endPosition) {
      mesh.userData.mixer = new THREE.AnimationMixer(mesh);
      const track = new THREE.VectorKeyframeTrack('.position', [0, 1, 2], [
        startPosition.x,
        startPosition.y,
        startPosition.z,
        endPosition.x,
        endPosition.y,
        endPosition.z,
      ]);

      const animationClip = new THREE.AnimationClip(null, 5, [track]);
      const animationAction = mesh.userData.mixer.clipAction(animationClip);
      animationAction.setLoop(THREE.LoopOnce);
      animationAction.play();
      mesh.userData.clock = new THREE.Clock();
      animations.push(mesh);
    };

    function createFireballMesh (type = 1) {
      const fireball = new THREE.Mesh(fireballGeometry, fireballMaterial);
      return fireball;
    }

    function createGhostMesh () {
      const ghost = new THREE.Mesh(vehicleGeometry, ghostMaterial);
      return ghost;
    }

    function createDialogue (content) {
      const buffer = new Uint8Array(32);
      const id = 'notification-' + Buffer.from(window.crypto.getRandomValues(buffer)).toString('hex');
      const site = document.querySelector('verse-dialogue-stack');
      const template = document.getElementById('dialogue-template');
      const clone = template.content.cloneNode(true);

      clone.querySelector('fabric-card').id = id;
      clone.querySelector('.typed-out').innerHTML = content;
      clone.querySelector('.button.dismiss').addEventListener('click', (event) => {
        const dialogue = document.getElementById(id);
        $(dialogue).fadeOut(1000);
      });

      site.append(clone);

      // TODO: remove jquery
      $(`#${id}`).slideDown();
    }

    function createVehicleMesh () {
      const vehicle = new THREE.Mesh(vehicleGeometry, vehicleMaterial);
      vehicle.add(vehicleGlow);
      return vehicle;
    }

    function createVoxelMesh (type = 1) {
      const geometry = new THREE.BoxGeometry(1, 1, 1);
      const edges = new THREE.EdgesGeometry(geometry);
      const lineMaterial = new THREE.LineBasicMaterial({ color: 0x000000 });
      const edgesMesh = new THREE.LineSegments(edges, lineMaterial);
      const material = new THREE.MeshLambertMaterial({ color: 0x00ff00 });
      const mesh = new THREE.Mesh(geometry, material);

      mesh.add(edgesMesh);
      mesh.userData.type = 'voxel';

      return mesh;
    }

    let plane = null;

    function drawBasePlane () {
      plane = createPlaneMesh();
      scene.add(plane);
    }

    function drawSpace () {
      // Initial voxels
      drawBasePlane();

      // Relevant meshes
      scene.add(vehicle);
      scene.add(ambient);
      // scene.add(spotlight);

      vehicle.position.x = STARTING_POSITION.x;
      vehicle.position.y = STARTING_POSITION.y + 0.05; // slightly above terrain
      vehicle.position.z = STARTING_POSITION.z;

      ghost.position.x = vehicle.position.x;
      ghost.position.y = vehicle.position.y;
      ghost.position.z = vehicle.position.z;

      // Spotlight
      spotlight.add(lightHelper);
      spotlight.position.set(vehicle.position.x, vehicle.position.y + 5, vehicle.position.z);
      spotlight.castShadow = true;
      spotlight.target = vehicle;

      // const island = generateFloatingIsland(50, 50, 50, 1, 20, 20, 42);
      // scene.add(island);
    }

    function getPixelValues (image) {
      const rgb24 = new Uint8Array((image.data.length / 4) * 3);

      var i = 0;
      var j = 0;

      while (i < image.data.length) {
        rgb24[j++] = image.data[i++];
        rgb24[j++] = image.data[i++];
        rgb24[j++] = image.data[i++];
        i++;

        if (rgb24[j] > 0) console.log('data:', rgb24[j]);
      }

      return rgb24;
    }

    function hideOverlay () {
      document.getElementById('overlay').style.display = 'none';
    }

    async function onDocumentKeyDown (event) {
      if (event.code === 'Escape') {
        $('#chat-input').fadeOut();
        $('#console').slideUp();
        isChatting = false;
      }

      if (isChatting) return true;

      const px = vehicle.position.x;
      const py = vehicle.position.y;
      const pz = vehicle.position.z;

      switch (event.code) {
        case 'KeyA':
        case 'ArrowLeft':
          event.preventDefault();
          // strafe left
          vehicle.position.z += 1;
          break;
        case 'KeyW':
        case 'ArrowUp':
          // strafe forward
          event.preventDefault();
          vehicle.position.x -= 1;
          break;
        case 'KeyD':
        case 'ArrowRight':
          // strafe right
          event.preventDefault();
          vehicle.position.z -= 1;
          break;
        case 'KeyS':
        case 'ArrowDown':
          // strafe back
          event.preventDefault();
          vehicle.position.x += 1;
          break;
        case 'Space':
          event.preventDefault();
          const fireball = createFireballMesh();
          const start = new THREE.Vector3(vehicle.position.x, vehicle.position.y, vehicle.position.z);
          const end = new THREE.Vector3(0, 1.34, 0);
          createMoveAnimation(fireball, start, end);
          scene.add(fireball);
          break;
        case 'Tab':
          event.preventDefault();
          console.log('tab pressed!  current camera mode:', currentCameraMode);
          if (currentCameraMode !== 'loading') {
            cameraModes.push(currentCameraMode);
            currentCameraMode = cameraModes.shift();
            console.log('mode:', currentCameraMode);
          }
          break;
        case 'Backquote':
          event.preventDefault();

          $('#overlay').fadeOut();
          $('#chat-input').fadeIn();
          $('#console').slideToggle();
          $('#console').animate({
            bottom: 0
          });

          $('#chat-input input').focus();

          isChatting = true;
          break;
        case 'KeyQ':
          // rotate camera left
          break;
        case 'KeyE':
          // rotate camera right
          break;
        case 'Escape':
          if (currentCameraMode !== 'loading') {
            event.preventDefault();
            $('#overlay').fadeToggle();
          }
          break;
      }

      // physics (lol)
      if (vehicle.position.x < 0) vehicle.position.x = px;
      if (vehicle.position.z < 0) vehicle.position.z = pz;
      if (vehicle.position.x >= SIZE) vehicle.position.x = px;
      if (vehicle.position.z >= SIZE) vehicle.position.z = pz;

      console.log('player position:', `${vehicle.position.x}:${vehicle.position.z}`);
      if (vehicle.position.x !== px || vehicle.position.y !== py || vehicle.position.z !== pz) {
        const encounter = await maybeEncounter();
        if (encounter) {
          console.log('[!!!] ENCOUNTER [!!!]');
          console.log(encounter);
        }
      }
    }

    function onDocumentClick (event) {
      // First, de-select any voxels on our baseplane
      const voxels = plane.children.filter((x) => (x.userData && x.userData.type === 'voxel'));

      for (let i = 0; i < voxels.length; i++) {
        voxels[i].userData.selected = false;
        for (let j = 0; j < voxels[i].children.length; j++) {
          voxels[i].children[j].material.color.setHex(0x000000);
        }
      }

      // Select the clicked cube by using the raycaster
      raycaster.setFromCamera(mouse, camera);

      const found = raycaster.intersectObjects(scene.children);
      const filtered = found.filter((x) => (x.object.userData && x.object.userData.type === 'voxel'));
      const selection = filtered.find((x) => (x.object.userData && x.object.userData.type === 'voxel'));

      if (!selection) return;

      event.preventDefault();
      console.log('selected:', selection);

      selection.object.userData.selected = true;

      // Highlight the edges
      for (let i = 0; i < selection.object.children.length; i++) {
        // selection.object.children[i].material.color.setHex(0xffffff);
        selection.object.children[i].add(selectionGlow);
      }

      selection.object.material.needsUpdate = true;

      return false;
    }

    function onDocumentMouseMove (event) {
      // the following line would stop any other event handler from firing
      // (such as the mouse's TrackballControls)
      // event.preventDefault();

      // update the mouse variable
      mouse.x = (event.clientX / window.innerWidth) * 2 - 1;
      mouse.y = -(event.clientY / window.innerHeight) * 2 + 1;
    }

    function onDocumentSwipeDown (event) {
        // strafe forward
        event.preventDefault();
        vehicle.position.x -= 1;
        vehicle.position.z -= 1;
    }

    function resizeCanvasToDisplaySize (force) {
      const canvas = renderer.domElement;
      const width = canvas.clientWidth;
      const height = canvas.clientHeight;

      if (force || canvas.width !== width || canvas.height !== height) {
        renderer.setSize(width, height, false);
        camera.aspect = width / height;
        camera.updateProjectionMatrix();
      }
    }

    // Main draw
    drawSpace();

    const raycaster = new THREE.Raycaster();

    for (let x = 0; x < SIZE; x++) {
      for (let y = 0; y < SIZE; y++) {
        const pixel = voxelMap[`${x}:${y}`];
        voxelMap[`${x}:${y}`].material.color = 0x000000;
      }
    }

    /* const gui = new dat.GUI();
    const cubeFolder = gui.addFolder('Location');

    gui.close();

    cubeFolder.add(vehicle.position, 'x', 0, SIZE - 1);
    cubeFolder.add(vehicle.position, 'z', 0, SIZE - 1); */

    // Attach
    document.body.appendChild(renderer.domElement);
    renderer.setSize(window.innerWidth, window.innerHeight);

    // Animate
    animate();

    window.addEventListener('deviceorientation', onDeviceOrientation, true);

    document.addEventListener('click', onDocumentClick, false);
    document.addEventListener('mousemove', onDocumentMouseMove, false);
    document.addEventListener('keydown', onDocumentKeyDown, false);
    document.addEventListener('swipedown', onDocumentSwipeDown, false);

    console.log('site:', site);

    document.getElementById('tray-settings').addEventListener('click', (event) => {
      $('#settings').fadeIn();
      return false;
    });

    document.getElementById('volume').addEventListener('click', (event) => {
      const element = document.getElementById('bgm');
      if (element.muted) {
        event.target.classList.remove('off');
        event.target.classList.add('up');
        document.getElementById('bgm').muted = false;
      } else {
        event.target.classList.remove('up');
        event.target.classList.add('off');
        document.getElementById('bgm').muted = true;
      }

      return false;
    });

    document.getElementById('connect').addEventListener('click', (event) => {
      document.getElementById('bgm').play();

      event.target.innerHTML = 'Connecting...';
      currentCameraMode = 'connecting';

      setTimeout(() => {
        event.target.innerHTML = 'Connected!';
        setTimeout(() => {
          $('#overlay').fadeOut();
          createDialogue('<strong>Wake up!</strong>');
        }, 1000);
      }, 2500);

      return false;
    });

    document.getElementById('chat-input').addEventListener('submit', function (event) {
      event.preventDefault();
    });

    document.querySelector('input[name=input]').addEventListener('blur', function (event) {
      isChatting = false;
    });

    document.getElementById('chat-close').addEventListener('click', function (event) {
      event.preventDefault();
      event.stopPropagation();

      $('#chat-input').fadeOut();
      $('#console').slideUp();

      isChatting = false;

      return false;
    });

    // const loadFromID = prompt('Character ID to load from:');
    const loadFromID = 1;
    const character = new Player();
    await character._loadFromCharacter(loadFromID);

    const location = new Place();
    await location._loadFromRPGByID(character.state.location);

    console.log('location:', location.state);

    // createDialogue('hi hi hi');
  });

  const engine = { id: null };

  return {
    engine: engine.id,
    universe: universe
  };
}

main().catch((exception) => {
  console.log('[VERSE] Error:', exception);
}).then((output) => {
  window.verse = output;
  console.log('[VERSE] Process Started:', output);
});
