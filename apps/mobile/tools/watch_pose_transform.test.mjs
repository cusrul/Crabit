import test from 'node:test';
import assert from 'node:assert/strict';
import * as THREE from '../assets/watch_sim/vendor/three.module.js';
import {
  attachPreservingWorldTransform,
  validPoseQuaternion,
  modelTiltQuaternion,
  poseBlendWeight,
} from '../assets/watch_sim/pose_transform.js';

test('model reverses vertical tilt without reversing horizontal tilt', () => {
  const roll = 0.4, pitch = 0.3;
  const input = new THREE.Quaternion().setFromEuler(new THREE.Euler(roll, pitch, 0, 'XYZ'));
  const mapped = new THREE.Quaternion(...modelTiltQuaternion(input.toArray()));
  const expected = new THREE.Quaternion().setFromEuler(new THREE.Euler(-roll, pitch, 0, 'XYZ'));
  assert.ok(mapped.angleTo(expected) < 1e-7);
});

test('frame-time slerp converges smoothly and is independent of frame rate', () => {
  const target = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), 1);
  const sample = hz => {
    const current = new THREE.Quaternion();
    for (let i = 0; i < hz / 5; i++) current.slerp(target, poseBlendWeight(1 / hz));
    return current;
  };
  const first = new THREE.Quaternion().slerp(target, poseBlendWeight(1 / 60));
  assert.ok(first.angleTo(target) > 0.1);
  assert.ok(first.angleTo(target) < 1);
  assert.ok(sample(30).angleTo(sample(60)) < 1e-7);
  assert.ok(sample(60).angleTo(target) < 0.1);
  assert.equal(poseBlendWeight(0), 0);
});

test('pose transform accepts normalized quaternions only', () => {
  assert.equal(validPoseQuaternion([0, 0, 0, 1]), true);
  assert.equal(validPoseQuaternion([0.2, 0.1, 0, 0.97]), true);
  assert.equal(validPoseQuaternion([0, 0, 0, 0]), false);
  assert.equal(validPoseQuaternion([0, 0, 0, 2]), false);
  assert.equal(validPoseQuaternion([0, 0, Number.NaN, 1]), false);
  assert.equal(validPoseQuaternion([0, 0, 1]), false);
});

test('OLED and pick helpers preserve world placement and follow the pose pivot', () => {
  const scene = new THREE.Scene();
  const pivot = new THREE.Group();
  pivot.position.set(2, 0, 0);
  const model = new THREE.Group();
  model.position.set(-2, 0, 0);
  pivot.add(model);
  scene.add(pivot);

  const panel = new THREE.Object3D();
  panel.position.set(2, 1, 0);
  const proxy = new THREE.Object3D();
  proxy.position.set(2, -1, 0);
  scene.add(panel, proxy);
  scene.updateMatrixWorld(true);
  const panelBefore = panel.getWorldPosition(new THREE.Vector3());
  const proxyBefore = proxy.getWorldPosition(new THREE.Vector3());

  attachPreservingWorldTransform(pivot, panel);
  attachPreservingWorldTransform(pivot, proxy);
  scene.updateMatrixWorld(true);
  assert.equal(panel.parent, pivot);
  assert.equal(proxy.parent, pivot);
  assert.ok(panel.getWorldPosition(new THREE.Vector3()).distanceTo(panelBefore) < 1e-9);
  assert.ok(proxy.getWorldPosition(new THREE.Vector3()).distanceTo(proxyBefore) < 1e-9);

  pivot.quaternion.setFromAxisAngle(new THREE.Vector3(0, 0, 1), Math.PI / 2);
  scene.updateMatrixWorld(true);
  const expectedPanel = new THREE.Vector3(2, 0, 0)
    .add(new THREE.Vector3(0, 1, 0).applyAxisAngle(new THREE.Vector3(0, 0, 1), Math.PI / 2));
  const expectedProxy = new THREE.Vector3(2, 0, 0)
    .add(new THREE.Vector3(0, -1, 0).applyAxisAngle(new THREE.Vector3(0, 0, 1), Math.PI / 2));
  assert.ok(panel.getWorldPosition(new THREE.Vector3()).distanceTo(expectedPanel) < 1e-9);
  assert.ok(proxy.getWorldPosition(new THREE.Vector3()).distanceTo(expectedProxy) < 1e-9);
});
