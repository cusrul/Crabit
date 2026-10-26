import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../assets/watch_sim/vendor/three.module.js';
import { pickVisibleButton } from '../assets/watch_sim/picking.js';

test('a padded key target cannot pierce a nearer visible surface', () => {
  const model = new THREE.Group();
  const button = new THREE.Group();
  const cap = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.3, 0.2), new THREE.MeshBasicMaterial());
  button.add(cap);
  model.add(button);
  const proxy = new THREE.Mesh(new THREE.BoxGeometry(0.8, 0.8, 2));
  proxy.userData.key = 2;
  proxy.updateMatrixWorld(true);
  const ray = new THREE.Raycaster(new THREE.Vector3(0, 0, 5), new THREE.Vector3(0, 0, -1));
  model.updateMatrixWorld(true);
  const buttons = [{ key: 2, node: button }];
  assert.equal(pickVisibleButton(ray, model, null, buttons, [proxy]), 2);

  // The blocker lies behind the proxy's front but in front of the actuator.
  const blocker = new THREE.Mesh(new THREE.BoxGeometry(0.4, 0.4, 0.1), new THREE.MeshBasicMaterial());
  blocker.position.z = 0.5;
  model.add(blocker);
  model.updateMatrixWorld(true);
  assert.equal(pickVisibleButton(ray, model, null, buttons, [proxy]), null);

  model.remove(blocker);
  ray.ray.origin.x = 0.3;
  // Broad phase intersects, but the actual physical key does not.
  assert.equal(pickVisibleButton(ray, model, null, buttons, [proxy]), null);
});
