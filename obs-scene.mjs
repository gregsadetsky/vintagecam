// builds an obs scene called "vintagecam" with one browser source per camera
// that vintageserve is streaming, laid out as a grid. needs node 22+ and obs
// with its websocket server on (obs: tools > websocket server settings).
// run it again after plugging in more cameras: existing sources are kept.
//   node obs-scene.mjs            password is read from obs's own config file
//   OBS_PASSWORD=... node obs-scene.mjs
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { homedir } from 'node:os';

const SERVE = process.env.VINTAGECAM_URL ?? 'http://127.0.0.1:8090';
const SCENE = 'vintagecam';

function password() {
  if (process.env.OBS_PASSWORD !== undefined) return process.env.OBS_PASSWORD;
  const path = `${homedir()}/Library/Application Support/obs-studio/plugin_config/obs-websocket/config.json`;
  return JSON.parse(readFileSync(path, 'utf8')).server_password ?? '';
}
const sha = (s) => createHash('sha256').update(s).digest('base64');

const cams = await (await fetch(`${SERVE}/cams.json`)).json();
if (!cams.length) {
  console.error('no cameras are being streamed, start the wall first (make wall)');
  process.exit(1);
}

const ws = new WebSocket('ws://127.0.0.1:4455');
const pending = new Map();
let nextId = 1;
const ready = new Promise((resolve, reject) => {
  ws.onerror = () => reject(new Error('cannot reach obs on port 4455: is the websocket server enabled?'));
  ws.onclose = (e) => reject(new Error(`obs closed the connection (${e.code} ${e.reason})`));
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.op === 0) {
      const d = { rpcVersion: 1 };
      const a = msg.d.authentication;
      if (a) d.authentication = sha(sha(password() + a.salt) + a.challenge);
      ws.send(JSON.stringify({ op: 1, d }));
    } else if (msg.op === 2) {
      resolve();
    } else if (msg.op === 7) {
      const p = pending.get(msg.d.requestId);
      pending.delete(msg.d.requestId);
      if (!p) return;
      if (msg.d.requestStatus.result) p.resolve(msg.d.responseData ?? {});
      else p.reject(new Error(`${msg.d.requestType}: ${msg.d.requestStatus.comment ?? msg.d.requestStatus.code}`));
    }
  };
});
function call(requestType, requestData = {}) {
  const requestId = String(nextId++);
  return new Promise((resolve, reject) => {
    pending.set(requestId, { resolve, reject });
    ws.send(JSON.stringify({ op: 6, d: { requestType, requestId, requestData } }));
  });
}

try {
  await ready;
  const { scenes } = await call('GetSceneList');
  if (!scenes.some((s) => s.sceneName === SCENE)) await call('CreateScene', { sceneName: SCENE });
  const { baseWidth, baseHeight } = await call('GetVideoSettings');
  const { inputs } = await call('GetInputList');
  const cols = cams.length <= 1 ? 1 : cams.length <= 4 ? 2 : cams.length <= 6 ? 3 : 4;
  const rows = Math.ceil(cams.length / cols);
  const cw = baseWidth / cols, ch = baseHeight / rows;

  for (const [i, cam] of cams.entries()) {
    const inputName = `vintagecam ${cam}`;
    const { sceneItems } = await call('GetSceneItemList', { sceneName: SCENE });
    let item = sceneItems.find((s) => s.sourceName === inputName);
    if (!item) {
      const settings = { url: `${SERVE}/cam/${cam}`, width: 640, height: 480, reroute_audio: false, shutdown: false };
      const r = inputs.some((x) => x.inputName === inputName)
        ? await call('CreateSceneItem', { sceneName: SCENE, sourceName: inputName })
        : await call('CreateInput', { sceneName: SCENE, inputName, inputKind: 'browser_source', inputSettings: settings });
      item = { sceneItemId: r.sceneItemId };
      console.log(`added ${inputName}`);
    } else {
      console.log(`kept  ${inputName}`);
    }
    await call('SetSceneItemTransform', {
      sceneName: SCENE,
      sceneItemId: item.sceneItemId,
      sceneItemTransform: {
        positionX: (i % cols) * cw, positionY: Math.floor(i / cols) * ch,
        boundsType: 'OBS_BOUNDS_SCALE_INNER', boundsWidth: cw, boundsHeight: ch, boundsAlignment: 0,
      },
    });
  }
  await call('SetCurrentProgramScene', { sceneName: SCENE });
  console.log(`scene "${SCENE}" has ${cams.length} camera(s), ${cols}x${rows}, canvas ${baseWidth}x${baseHeight}`);
  ws.onclose = null;
  ws.close();
} catch (e) {
  console.error(e.message);
  process.exit(1);
}
