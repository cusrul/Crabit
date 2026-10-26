import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { bindings } from './model_bindings.js';
import { pickVisibleButton } from './picking.js';
import {
  attachPreservingWorldTransform,
  validPoseQuaternion,
  modelTiltQuaternion,
  poseBlendWeight,
} from './pose_transform.js';

const status = document.querySelector('#status');
const scene = new THREE.Scene();
scene.background = new THREE.Color('#f3f5f7');
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.domElement.tabIndex = 0;
renderer.domElement.setAttribute('aria-label', 'Crabit 手表预览；方向键操作菜单，Enter 确认');
document.body.prepend(renderer.domElement);
const camera = new THREE.PerspectiveCamera(36, 1, 0.01, 1000);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enablePan = false;
controls.enableDamping = true;
controls.enabled = false;
controls.enableRotate = false;
controls.enableZoom = false;
scene.add(new THREE.HemisphereLight(0xffffff, 0x9099a3, 2.4));
const light = new THREE.DirectionalLight(0xffffff, 2.5);
light.position.set(-3, 6, -8);
scene.add(light);

const canvas = document.createElement('canvas');
canvas.width = 128;
canvas.height = 64;
const ctx = canvas.getContext('2d', { alpha: false });
ctx.fillStyle = 'black';
ctx.fillRect(0, 0, 128, 64);
const pixels = ctx.createImageData(128, 64);
const texture = new THREE.CanvasTexture(canvas);
texture.colorSpace = THREE.SRGBColorSpace;
texture.magFilter = THREE.NearestFilter;
texture.minFilter = THREE.NearestFilter;
texture.generateMipmaps = false;
const buttons = [];
const pickables = [];
const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
const heldKeys = new Map();
const keyboardButtons = new Map([
  ['ArrowUp', 0], ['ArrowDown', 1], ['Enter', 2],
]);
let active = null;
let sequence = -1;
let frameCount = 0;
let disposed = false;
let polling = false;
let model;
let poseRoot;
let screenPanel;
let presentationActive = true;
let inspectMode = false;
let loaded = false;
let animationHandle = null;
let interactiveUi = false;
let followMode = false;
const targetPose = new THREE.Quaternion();

async function notify(handler, ...args) {
  if (!window.flutter_inappwebview?.callHandler) return;
  return window.flutter_inappwebview.callHandler(handler, ...args);
}

function fail(error) {
  status.textContent = `预览无法运行：${error.message ?? error}`;
  status.hidden = false;
  status.style.display = 'grid';
  void notify('nanoError', String(error));
}
window.addEventListener('error', event => fail(event.error ?? event.message));
window.addEventListener('unhandledrejection', event => fail(event.reason));

function named(root, name) {
  let result;
  root.traverse(node => {
    if (node.name.replaceAll('.', '') === name.replaceAll('.', '')) result = node;
  });
  if (!result) throw new Error(`模型缺少部件 ${name}`);
  return result;
}

function boxProxy(bounds, key = null) {
  const size = bounds.getSize(new THREE.Vector3());
  const object = new THREE.Mesh(new THREE.BoxGeometry(size.x, size.y, size.z));
  object.position.copy(bounds.getCenter(new THREE.Vector3()));
  object.visible = false;
  object.userData.key = key;
  scene.add(object);
  attachPreservingWorldTransform(poseRoot, object);
  object.updateMatrixWorld(true);
  pickables.push(object);
  return object;
}

function attachScreen(root) {
  const screen = named(root, bindings.screen);
  const bounds = new THREE.Box3().setFromObject(screen);
  screen.traverse(node => {
    if (node.isMesh) node.material = new THREE.MeshBasicMaterial({ color: 0x050505 });
  });
  const width = bounds.getSize(new THREE.Vector3()).x * bindings.screenWidthFraction;
  // OLED has a 2:1 active area. Keep the CAD housing and give only its front
  // surface an unlit pixel plane, independent of the old image's UV rotation.
  const panel = new THREE.Mesh(new THREE.PlaneGeometry(width, width / 2),
    new THREE.MeshBasicMaterial({ map: texture, toneMapped: false }));
  panel.position.copy(bounds.getCenter(new THREE.Vector3()));
  panel.position.z = bounds.min.z - 0.003;
  panel.rotation.y = Math.PI;
  scene.add(panel);
  attachPreservingWorldTransform(poseRoot, panel);
  screenPanel = panel;
}

function attachButtons(root) {
  for (const binding of bindings.buttons) {
    const node = named(root, binding.mesh);
    let cap;
    node.traverse(child => {
      // In this CAD export mat_28 is the protruding actuator. Keep solder
      // contacts and switch base stationary while the actuator moves.
      if (child.isMesh && child.material?.name.startsWith('mat_28')) cap = child;
    });
    if (!cap) throw new Error(`模型缺少按帽 ${binding.mesh}`);
    const bounds = new THREE.Box3().setFromObject(node);
    bounds.expandByScalar(0.10);
    const proxy = boxProxy(bounds, binding.key);
    buttons.push({ ...binding, node, cap, proxy, origin: cap.position.clone(),
      axis: new THREE.Vector3(...binding.inward), amount: 0, target: 0 });
  }
}

function pick(event) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.set((event.clientX - rect.left) / rect.width * 2 - 1,
    -(event.clientY - rect.top) / rect.height * 2 + 1);
  raycaster.setFromCamera(pointer, camera);
  return pickVisibleButton(raycaster, model, screenPanel, buttons, pickables);
}

function release(cancel = false) {
  if (!active) return;
  const { key } = active;
  active = null;
  buttons.find(button => button.key === key).target = 0;
  controls.enabled = presentationActive && inspectMode && !followMode;
  void notify('nanoButton', key, cancel ? -1 : 0);
}
function releaseKeyboard(cancel = true) {
  for (const key of heldKeys.values()) void notify('nanoButton', key, cancel ? -1 : 0);
  heldKeys.clear();
}
renderer.domElement.addEventListener('keydown', event => {
  if (!interactiveUi || !presentationActive || event.repeat) return;
  const key = keyboardButtons.get(event.key);
  if (key === undefined) return;
  event.preventDefault();
  heldKeys.set(event.code || event.key, key);
  void notify('nanoButton', key, 1);
});
renderer.domElement.addEventListener('keyup', event => {
  const id = event.code || event.key;
  const key = heldKeys.get(id);
  if (key === undefined) return;
  event.preventDefault();
  heldKeys.delete(id);
  void notify('nanoButton', key, 0);
});
renderer.domElement.addEventListener('pointerdown', () => {
  renderer.domElement.focus({ preventScroll: true });
}, true);
renderer.domElement.addEventListener('pointerdown', event => {
  if (!presentationActive || !interactiveUi) return;
  if (event.button !== 0) return;
  if (active) { release(true); return; }
  const key = pick(event);
  if (key === null) return;
  event.stopImmediatePropagation();
  controls.enabled = false;
  active = { key, pointer: event.pointerId, x: event.clientX, y: event.clientY };
  renderer.domElement.setPointerCapture(event.pointerId);
  buttons.find(button => button.key === key).target = 1;
  void notify('nanoButton', key, 1);
}, true);
renderer.domElement.addEventListener('pointermove', event => {
  if (active?.pointer === event.pointerId &&
      Math.hypot(event.clientX - active.x, event.clientY - active.y) > 12 &&
      pick(event) !== active.key) release(true);
});
renderer.domElement.addEventListener('pointerup', event => {
  if (active?.pointer === event.pointerId) release(false);
});
renderer.domElement.addEventListener('pointercancel', () => release(true));
renderer.domElement.addEventListener('lostpointercapture', () => release(true));
window.addEventListener('blur', () => { release(true); releaseKeyboard(true); });
renderer.domElement.addEventListener('wheel', event => {
  if (presentationActive && !inspectMode) {
    event.preventDefault();
    const delta = event.deltaY * (event.deltaMode === 1 ? 20 : event.deltaMode === 2 ? innerHeight : 1);
    void notify('nanoScroll', delta);
  }
}, { passive: false });
document.addEventListener('visibilitychange', () => {
  if (document.hidden) { release(true); releaseKeyboard(true); }
});

async function pullFrame() {
  if (!interactiveUi || polling || disposed || !presentationActive || document.hidden) return;
  polling = true;
  try {
    const response = await fetch(`./frame?after=${sequence}`, { cache: 'no-store' });
    if (response.status === 204) return;
    if (!response.ok) throw new Error(`画面读取失败 ${response.status}`);
    const data = new Uint8Array(await response.arrayBuffer());
    if (data.length !== 1024) throw new Error('画面长度错误');
    sequence = Number(response.headers.get('X-Frame-Sequence'));
    for (let i = 0; i < 8192; i++) {
      const value = data[i >> 3] & (128 >> (i & 7)) ? 245 : 0;
      const offset = i * 4;
      pixels.data[offset] = pixels.data[offset + 1] = pixels.data[offset + 2] = value;
      pixels.data[offset + 3] = 255;
    }
    ctx.putImageData(pixels, 0, 0);
    texture.needsUpdate = true;
    frameCount++;
  } catch (error) { if (!disposed) fail(error); }
  finally { polling = false; }
}

let last = performance.now();
let lastPull = 0;
function animate(now) {
  animationHandle = null;
  if (disposed || !presentationActive) return;
  const dt = Math.min((now - last) / 1000, 0.1);
  last = now;
  if (poseRoot && followMode) {
    poseRoot.quaternion.slerp(targetPose, poseBlendWeight(dt));
  }
  for (const button of buttons) {
    button.amount += (button.target - button.amount) * (1 - Math.exp(-dt * 32));
    button.cap.position.copy(button.origin).addScaledVector(button.axis, button.amount * 0.055);
  }
  controls.update();
  renderer.render(scene, camera);
  if (now - lastPull >= 33) { lastPull = now; void pullFrame(); }
  animationHandle = requestAnimationFrame(animate);
}
function resize() {
  const width = innerWidth, height = innerHeight;
  renderer.setSize(width, height);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
}
window.addEventListener('resize', resize);
window.addEventListener('pagehide', () => {
  release(true);
  releaseKeyboard(true);
  disposed = true;
  if (animationHandle !== null) cancelAnimationFrame(animationHandle);
  controls.dispose();
  renderer.dispose();
});

// Small diagnostic surface for the desktop acceptance check. Input itself goes
// through real pointer events; this exposes observed state rather than a fake UI.
window.crabitPreview = {
  setPresentation: ({ active: visible, inspect, follow = false }) => {
    const changedMode = inspectMode !== inspect;
    inspectMode = inspect;
    followMode = follow;
    presentationActive = visible;
    controls.enableRotate = inspect;
    controls.enableZoom = inspect;
    if (changedMode && !inspect && loaded) controls.reset();
    if (!visible) {
      release(true);
      if (animationHandle !== null) cancelAnimationFrame(animationHandle);
      animationHandle = null;
    } else if (loaded && animationHandle === null) {
      last = performance.now();
      animationHandle = requestAnimationFrame(animate);
    }
    controls.enabled = visible && inspect && !follow && active === null;
  },
  setPose: (x, y, z, w, snap = false) => {
    const components = [x, y, z, w];
    if (!validPoseQuaternion(components) || !poseRoot) return false;
    targetPose.set(...modelTiltQuaternion(components)).normalize();
    if (snap || !followMode) poseRoot.quaternion.copy(targetPose);
    return true;
  },
  stats: () => ({ sequence, frameCount, buttons: buttons.map(b => ({
    key: b.key, amount: b.amount, target: b.target,
  })) }),
};

try {
  const mode = await fetch('./mode', { cache: 'no-store' });
  if (!mode.ok) throw new Error('预览模式无法读取');
  interactiveUi = (await mode.json()).interactive === true;
  const gltf = await new GLTFLoader().loadAsync('./model.glb');
  model = gltf.scene;
  model.updateMatrixWorld(true);
  const initialBounds = new THREE.Box3().setFromObject(model);
  const pivot = initialBounds.getCenter(new THREE.Vector3());
  poseRoot = new THREE.Group();
  poseRoot.position.copy(pivot);
  model.position.sub(pivot);
  poseRoot.add(model);
  scene.add(poseRoot);
  poseRoot.updateMatrixWorld(true);
  if (interactiveUi) { attachScreen(model); attachButtons(model); }
  const bounds = new THREE.Box3().setFromObject(model);
  const center = bounds.getCenter(new THREE.Vector3());
  const size = bounds.getSize(new THREE.Vector3());
  const distance = Math.max(size.x, size.y) / (2 * Math.tan(THREE.MathUtils.degToRad(18))) * 1.30;
  camera.position.copy(center).add(new THREE.Vector3(0, 0.1, -distance));
  controls.target.copy(center);
  controls.minDistance = distance * 0.5;
  controls.maxDistance = distance * 2.5;
  controls.update();
  controls.saveState();
  resize();
  status.hidden = true;
  // CSS grid would override the UA hidden rule.
  status.style.display = 'none';
  await pullFrame();
  loaded = true;
  if (presentationActive) animationHandle = requestAnimationFrame(animate);
  await notify('nanoReady');
} catch (error) { fail(error); }
