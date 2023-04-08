'use strict'

const SIZE = 32;
const THREE = require('three');
const scene = new THREE.Scene();

async function main () {
  const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.1, 1000);
  const renderer = new THREE.WebGLRenderer({ antialias: true });

  function createVoxelMesh (type = 1) {
    const geometry = new THREE.BoxGeometry(1, 1, 1);
    const material = new THREE.MeshBasicMaterial({ color: 0x00ff00 });
    const cube = new THREE.Mesh(geometry, material);
    return cube;
  }

  // Initial voxels
  for (let i = 0; i < SIZE; i++) {
    for (let j = 0; j < SIZE; j++) {
      const p = createVoxelMesh();
      scene.add(p);
      p.position.set(i, 0, j);
    }
  }

  scene.add(camera);

  document.body.appendChild(renderer.domElement);
  renderer.setSize(window.innerWidth, window.innerHeight);
}

main().catch((exception) => {
  console.error('Main Process Exception:', exception);
}).then((output) => {
  console.log('Main Process Output:', output);
});
