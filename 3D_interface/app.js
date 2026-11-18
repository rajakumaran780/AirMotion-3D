(() => {
  "use strict";

  // ---------- Settings ----------
  const WS_URL = "ws://192.168.4.1:81/ws";
  const H_SIGN = -1;        // flip to 1 if left/right rotation is reversed
  const V_SIGN = 1;         // flip to -1 if up/down rotation is reversed
  const SMOOTHING = 0.6;
  const STALE_MS = 3000;

  const $ = (id) => document.getElementById(id);
  const rad = (d) => d * Math.PI / 180;
  const shape = (v, dz) => { const m = Math.abs(v) - dz; return m > 0 ? Math.sign(v) * m : 0; };

  // ---------- State ----------
  let mode3D = false, touching = false, connected = false;
  let sh = 0, sv = 0;
  let lastMotionT = 0, lastMsgT = 0, msgCount = 0;
  let latest = { pitch: 0, roll: 0, yaw: 0, gx: 0, gz: 0 };
  let selected = null;
  let ws = null;
  let gestureTimer = null;

  // ---------- Scene ----------
  const canvas = $("scene");
  canvas.style.touchAction = "none";
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setClearColor(0x070b14, 1);

  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0x070b14, 0.03);

  const camera = new THREE.PerspectiveCamera(55, 1, 0.1, 200);
  camera.position.set(0, 1.4, 7.5);
  camera.lookAt(0, 0, 0);

  scene.add(new THREE.AmbientLight(0x6f86b8, 0.7));
  const keyLight = new THREE.DirectionalLight(0xffffff, 1.1);
  keyLight.position.set(4, 6, 5);
  scene.add(keyLight);
  const fillLight = new THREE.PointLight(0x3d8bff, 1.2, 30);
  fillLight.position.set(-5, 2, 3);
  scene.add(fillLight);

  // Two independent groups: environmentGroup (whole world) and objectGroup (selectable objects)
  const environmentGroup = new THREE.Group();
  scene.add(environmentGroup);
  const objectGroup = new THREE.Group();
  environmentGroup.add(objectGroup);

  // Environment: grid, rings, stars
  const grid = new THREE.GridHelper(40, 40, 0x2f6fe0, 0x16304f);
  grid.position.y = -1.9;
  environmentGroup.add(grid);

  [4.6, 7.2].forEach((r, i) => {
    const ring = new THREE.Mesh(
      new THREE.TorusGeometry(r, 0.012, 8, 160),
      new THREE.MeshBasicMaterial({ color: 0x3d8bff, transparent: true, opacity: 0.45 - i * 0.15 })
    );
    ring.rotation.x = Math.PI / 2;
    ring.position.y = -1.6;
    environmentGroup.add(ring);
  });

  const starPositions = [];
  for (let i = 0; i < 900; i++) {
    const r = 18 + Math.random() * 22;
    const th = Math.random() * Math.PI * 2;
    const ph = Math.acos(2 * Math.random() - 1);
    starPositions.push(r * Math.sin(ph) * Math.cos(th), r * Math.cos(ph), r * Math.sin(ph) * Math.sin(th));
  }
  const starGeo = new THREE.BufferGeometry();
  starGeo.setAttribute("position", new THREE.Float32BufferAttribute(starPositions, 3));
  const stars = new THREE.Points(
    starGeo,
    new THREE.PointsMaterial({ color: 0x8fb4ff, size: 0.08, transparent: true, opacity: 0.8, fog: false })
  );
  environmentGroup.add(stars);

  // Selectable objects
  const defs = [
    { name: "CUBE", geo: new THREE.BoxGeometry(1.3, 1.3, 1.3), color: 0x3d8bff, x: -2.8 },
    { name: "TORUS KNOT", geo: new THREE.TorusKnotGeometry(0.7, 0.22, 120, 16), color: 0xff7a45, x: 0 },
    { name: "ICOSAHEDRON", geo: new THREE.IcosahedronGeometry(0.95, 0), color: 0x2ee59d, x: 2.8 }
  ];
  const meshes = defs.map((d, i) => {
    const m = new THREE.Mesh(
      d.geo,
      new THREE.MeshStandardMaterial({ color: d.color, metalness: 0.45, roughness: 0.3, emissive: 0x000000 })
    );
    m.position.set(d.x, 0, 0);
    m.userData = { name: d.name, phase: i * 1.7 };
    objectGroup.add(m);
    return m;
  });

  function select(mesh) {
    selected = mesh;
    meshes.forEach((m) => m.material.emissive.setHex(m === mesh ? 0x24407a : 0x000000));
    $("v-sel").textContent = mesh ? mesh.userData.name : "-";
  }
  select(meshes[0]);

  // Mini device-orientation view
  const miniScene = new THREE.Scene();
  const miniCam = new THREE.PerspectiveCamera(40, 1, 0.1, 50);
  miniCam.position.set(0, 0, 5.5);
  miniScene.add(new THREE.AmbientLight(0xffffff, 0.8));
  const miniLight = new THREE.DirectionalLight(0xffffff, 0.9);
  miniLight.position.set(2, 3, 4);
  miniScene.add(miniLight);
  const miniBox = new THREE.Mesh(
    new THREE.BoxGeometry(2.2, 0.4, 1.2),
    new THREE.MeshStandardMaterial({ color: 0x3d8bff, metalness: 0.3, roughness: 0.4 })
  );
  const led = new THREE.Mesh(new THREE.SphereGeometry(0.1, 12, 12), new THREE.MeshBasicMaterial({ color: 0xff4d5e }));
  led.position.set(0.8, 0.25, 0);
  miniBox.add(led);
  miniBox.rotation.order = "YXZ";
  miniScene.add(miniBox);
  miniScene.add(new THREE.AxesHelper(1.8));

  // ---------- Resize / render loop ----------
  function resize() {
    const w = window.innerWidth, h = window.innerHeight;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  window.addEventListener("resize", resize);
  resize();

  const clock = new THREE.Clock();
  function animate() {
    requestAnimationFrame(animate);
    const t = clock.getElapsedTime();

    meshes.forEach((m) => { m.position.y = Math.sin(t * 1.1 + m.userData.phase) * 0.12; });
    stars.rotation.y = t * 0.01;
    miniBox.rotation.set(rad(latest.pitch), rad(latest.yaw), rad(latest.roll));

    const w = window.innerWidth, h = window.innerHeight;
    renderer.setScissorTest(false);
    renderer.setViewport(0, 0, w, h);
    renderer.render(scene, camera);

    const s = 170, m = 20;
    renderer.setScissorTest(true);
    renderer.setViewport(w - s - m, m, s, s);
    renderer.setScissor(w - s - m, m, s, s);
    renderer.render(miniScene, miniCam);
    renderer.setScissorTest(false);
  }
  animate();

  // ---------- UI helpers ----------
  function setConn(ok) {
    connected = ok;
    const el = $("conn");
    el.className = "pill " + (ok ? "ok" : "bad");
    el.querySelector("b").textContent = ok ? "CONNECTED" : "DISCONNECTED";
    if (!ok) setTouch(false);
  }

  function setTouch(on) {
    touching = on;
    $("touch").classList.toggle("on", on);
  }

  function setMode(is3D) {
    if (is3D === mode3D && $("hint").textContent) return;
    mode3D = is3D;
    sh = 0; sv = 0;
    const el = $("mode");
    el.classList.toggle("m3d", is3D);
    el.querySelector("b").textContent = is3D ? "3D MODE" : "MOUSE MODE";
    $("hint").textContent = is3D
      ? "3D MODE: turn your finger to rotate the selected object. Touch and hold the pad to rotate the whole environment. Double-tap to exit."
      : "MOUSE MODE: tap an object to select it. Double-tap to enter 3D mode.";
  }
  setMode(false);

  function flashGesture(action) {
    $("v-gesture").textContent = action.replace("_", " ");
    clearTimeout(gestureTimer);
    gestureTimer = setTimeout(() => { $("v-gesture").textContent = "-"; }, 1200);
  }

  // ---------- Data handling ----------
  function handleMotion(d) {
    msgCount++;
    setMode(d.mode === "3D");
    setTouch(!!d.touch);
    latest = d;

    $("v-pitch").textContent = d.pitch.toFixed(1) + "\u00B0";
    $("v-roll").textContent = d.roll.toFixed(1) + "\u00B0";
    $("v-yaw").textContent = d.yaw.toFixed(1) + "\u00B0";
    $("v-gx").textContent = (d.gx || 0).toFixed(1);
    $("v-gz").textContent = (d.gz || 0).toFixed(1);

    const now = performance.now();
    const dt = lastMotionT ? Math.min((now - lastMotionT) / 1000, 0.1) : 0.02;
    lastMotionT = now;

    if (!mode3D) { sh = 0; sv = 0; return; }

    sh += SMOOTHING * ((d.gx || 0) - sh);
    sv += SMOOTHING * ((d.gz || 0) - sv);
    const dz = parseFloat($("dz").value);
    const h = shape(sh, dz) * H_SIGN;
    const v = shape(sv, dz) * V_SIGN;

    const target = touching ? environmentGroup : selected;
    const gain = parseFloat(touching ? $("envGain").value : $("objGain").value);
    if (target) {
      target.rotation.y += rad(h) * gain * dt;
      target.rotation.x += rad(v) * gain * dt;
    }
  }

  function handle(d) {
    if (d.type === "motion") handleMotion(d);
    else if (d.type === "mode") setMode(d.mode === "3D");
    else if (d.type === "gesture") flashGesture(d.action);
  }

  // ---------- WebSocket ----------
  function connect() {
    try { ws = new WebSocket(WS_URL); }
    catch (e) { setTimeout(connect, 2000); return; }

    ws.onopen = () => { setConn(true); lastMsgT = performance.now(); };
    ws.onmessage = (ev) => {
      lastMsgT = performance.now();
      let d;
      try { d = JSON.parse(ev.data); } catch (e) { return; }
      handle(d);
    };
    ws.onclose = () => { setConn(false); setTimeout(connect, 2000); };
    ws.onerror = () => { try { ws.close(); } catch (e) {} };
  }
  connect();

  // Drop a silent connection so it can reconnect
  setInterval(() => {
    if (connected && performance.now() - lastMsgT > STALE_MS) {
      try { ws.close(); } catch (e) {}
    }
  }, 1000);

  // Link rate display
  setInterval(() => {
    $("v-rate").textContent = msgCount + " Hz";
    msgCount = 0;
  }, 1000);

  // ---------- Mouse / touchpad: select, drag to rotate, wheel to zoom ----------
  const raycaster = new THREE.Raycaster();
  const pointer = new THREE.Vector2();
  let dragging = false, dragButton = 0, lastX = 0, lastY = 0;

  function pick(ev) {
    const rect = canvas.getBoundingClientRect();
    pointer.x = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
    pointer.y = -((ev.clientY - rect.top) / rect.height) * 2 + 1;
    raycaster.setFromCamera(pointer, camera);
    const hits = raycaster.intersectObjects(meshes);
    return hits.length ? hits[0].object : null;
  }

  canvas.addEventListener("pointerdown", (ev) => {
    dragging = true;
    dragButton = ev.button;
    lastX = ev.clientX;
    lastY = ev.clientY;
    try { canvas.setPointerCapture(ev.pointerId); } catch (e) {}
    const hit = pick(ev);
    if (hit && ev.button === 0) select(hit);
  });

  canvas.addEventListener("pointermove", (ev) => {
    if (!dragging) return;
    if (ev.buttons === 0) { dragging = false; return; }
    const dx = ev.clientX - lastX;
    const dy = ev.clientY - lastY;
    lastX = ev.clientX;
    lastY = ev.clientY;
    const target = (dragButton === 2 || ev.shiftKey) ? environmentGroup : selected;
    if (!target) return;
    target.rotation.y += dx * 0.01;
    target.rotation.x += dy * 0.01;
  });

  const endDrag = (ev) => {
    dragging = false;
    try { canvas.releasePointerCapture(ev.pointerId); } catch (e) {}
  };
  canvas.addEventListener("pointerup", endDrag);
  canvas.addEventListener("pointercancel", endDrag);
  canvas.addEventListener("contextmenu", (ev) => ev.preventDefault());

  canvas.addEventListener("wheel", (ev) => {
    ev.preventDefault();
    camera.position.z = Math.min(16, Math.max(3.5, camera.position.z + ev.deltaY * 0.005));
  }, { passive: false });

  // ---------- Controls ----------
  const bindLabel = (id, labelId, fmt) => {
    const el = $(id), lab = $(labelId);
    const update = () => { lab.textContent = fmt(el.value); };
    el.addEventListener("input", update);
    update();
  };
  bindLabel("objGain", "l-obj", (v) => parseFloat(v).toFixed(1) + "x");
  bindLabel("envGain", "l-env", (v) => parseFloat(v).toFixed(1) + "x");
  bindLabel("dz", "l-dz", (v) => v + " deg/s");

  function resetView() {
    environmentGroup.rotation.set(0, 0, 0);
    meshes.forEach((m) => m.rotation.set(0, 0, 0));
  }

  $("btn-center").addEventListener("click", () => {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send("recenter");
  });
  $("btn-reset").addEventListener("click", resetView);

  // Keyboard fallback for testing without the device
  window.addEventListener("keydown", (e) => {
    const step = 0.08;
    const target = e.shiftKey ? environmentGroup : selected;
    if (!target) return;
    if (e.key === "ArrowLeft") target.rotation.y -= step;
    else if (e.key === "ArrowRight") target.rotation.y += step;
    else if (e.key === "ArrowUp") target.rotation.x -= step;
    else if (e.key === "ArrowDown") target.rotation.x += step;
    else if (e.key === "r" || e.key === "R") resetView();
  });
})();