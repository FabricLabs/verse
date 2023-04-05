'use strict';

const {
  SIZE,
  STARTING_POSITION
} = require('../constants');

const THREE = require('three');
const dat = require('dat.gui');

const { OrbitControls } = require('three/examples/jsm/controls/OrbitControls');
const { ImprovedNoise } = require('three/examples/jsm/math/ImprovedNoise');

const Sheet = require('../types/sheet');

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

  window.addEventListener('load', () => {
    console.log('loaded!');

    // Three.js elements
    const clock = new THREE.Clock();
    const scene = new THREE.Scene();
    const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
    const renderer = new THREE.WebGLRenderer({
      alpha: true,
      antialias: true,
      precision: 'highp'
    });

    const mouse = {
      x: 0,
      y: 0
    };

    // Rover
    const vehicleGeometry = new THREE.SphereGeometry(0.5, 32, 32);
    const vehicleMaterial = new THREE.MeshBasicMaterial({ color: 0x2185d0 });
    const vehicleGlow = new THREE.PointLight(0x5555ee, 1, 50);

    // Fireballs
    const fireballGeometry = new THREE.SphereGeometry(0.05, 8, 8);
    const fireballMaterial = new THREE.MeshBasicMaterial({ color: 0xFF0000 });

    // Lighting
    const ambient = new THREE.AmbientLight(0x404040);
    const spotlight = new THREE.SpotLight(0xffffff);
    const lightHelper = new THREE.SpotLightHelper(spotlight);

    // Voxels
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
    scene.add(vehicle);

    // Component state
    const animations = [];
    const state = [];

    let cameraModes = ['isometric', 'overhead', 'first-person'];
    let currentCameraMode = 'loading';

    function animate () {
      const time = clock.getElapsedTime();

      requestAnimationFrame(animate);
      resizeCanvasToDisplaySize();

      switch (currentCameraMode) {
        default:
        case 'isometric':
          camera.position.set(vehicle.position.x + 5.2, vehicle.position.y + 5.2, vehicle.position.z + 5.2);
          camera.lookAt(vehicle.position.x -2, Math.floor(vehicle.position.y) - 2, vehicle.position.z - 2);
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
          group.add(p);
          p.position.set(i, 0, j);
        }
      }

      return group;
    }

    function createMoveAnimation(mesh, startPosition, endPosition) {
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

      vehicle.position.x = STARTING_POSITION.x;
      vehicle.position.y = STARTING_POSITION.y + 0.05; // slightly above terrain
      vehicle.position.z = STARTING_POSITION.z;

      // const island = generateFloatingIsland(50, 50, 50, 1, 20, 20, 42);
      // scene.add(island);

      // Camera start
      camera.position.x = 4;
      camera.position.y = 5;
      camera.position.z = 4;

      camera.lookAt(0, 1, 0);
    }

    function onDocumentKeyDown (event) {
      const px = vehicle.position.x;
      const py = vehicle.position.y;
      const pz = vehicle.position.z;

      switch (event.code) {
        case 'ArrowLeft':
          event.preventDefault();
          // strafe left
          vehicle.position.x -= 1;
          vehicle.position.z += 1;
          break;
        case 'ArrowUp':
          // strafe forward
          event.preventDefault();
          vehicle.position.x -= 1;
          vehicle.position.z -= 1;
          break;
        case 'ArrowRight':
          // strafe right
          event.preventDefault();
          vehicle.position.x += 1;
          vehicle.position.z -= 1;
          break;
        case 'ArrowDown':
          // strafe back
          event.preventDefault();
          vehicle.position.x += 1;
          vehicle.position.z += 1;
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
          console.log('tab pressed!');
          if (currentCameraMode !== 'loading') {
            cameraModes.push(currentCameraMode);
            currentCameraMode = cameraModes.shift();
            console.log('mode:', currentCameraMode);
          }
          break;
        case 'KeyQ':
          // rotate camera left
          break;
        case 'KeyE':
          // rotate camera right
          break;
      }

      // physics (lol)
      if (vehicle.position.x < 0) vehicle.position.x = px;
      if (vehicle.position.z < 0) vehicle.position.z = pz;
      if (vehicle.position.x >= SIZE) vehicle.position.x = px;
      if (vehicle.position.z >= SIZE) vehicle.position.z = pz;
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

    const gui = new dat.GUI();
    const cubeFolder = gui.addFolder('Location');

    gui.close();

    cubeFolder.add(vehicle.position, 'x', 0, SIZE - 1);
    cubeFolder.add(vehicle.position, 'z', 0, SIZE - 1);

    // Attach
    document.body.appendChild(renderer.domElement);
    renderer.setSize(window.innerWidth, window.innerHeight);

    // Animate
    animate();

    document.addEventListener('mousemove', onDocumentMouseMove, false);
    document.addEventListener('keydown', onDocumentKeyDown, false);

    console.log('site:', site);
  });

  const engine = { id: null };

  return {
    engine: engine.id
  };
}

main().catch((exception) => {
  console.log('[VERSE] Error:', exception);
}).then((output) => {
  console.log('[VERSE] Process Started:', output);
});
