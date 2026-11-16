/* SPDX-License-Identifier: GPL-3.0-only */
/* Mock MIDI device adapted from BB1 r85 test_return_official.mjs. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { Updater, pack7, unpack7 } from "../web/installer/fm1ota.js";
import { sha256hex } from "../web/installer/fm1pkg.js";
import { COMPAS_PRODUCT, STARTING_PRODUCTS, validateCompasPackage, validateOfficialReturn }
  from "../web/installer/compas-policy.mjs";

const [fixturePath, stockPath, bb1Path] = process.argv.slice(2);
assert(fixturePath && stockPath && bb1Path, "Pass an inert Compás fixture, official V15 and BB1 r85.");
const bytes = readFileSync(fixturePath);
const sha = await sha256hex(bytes);
const compas = await validateCompasPackage(bytes, sha);
const stock = await validateOfficialReturn(readFileSync(stockPath));
const bb1 = readFileSync(bb1Path);
await assert.rejects(validateCompasPackage(bb1, await sha256hex(bb1)), /identity differs/);
await assert.rejects(validateCompasPackage(bytes, "0".repeat(64)), /checksum differs/);
await assert.rejects(validateCompasPackage(bytes), /hash is required/);

const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const HS_FULL = [0xF0, 0x00, 0x32, 0x45, 0x08, 0x00, 0x00, 0x40, 0x00, 0x7E, 0x01, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

class FakeFM1 {
  constructor(image, { startIdentity = "FM-1_92200201", targetIdentity = "FM-1_015",
      unplugAfter = Infinity, extendedHandshake = false } = {}) {
    this.image = image; this.targetIdentity = targetIdentity; this.unplugAfter = unplugAfter;
    this.extendedHandshake = extendedHandshake; this.fullQueries = 0; this.bb1FullQueries = 0;
    this.served = 0; this.bad = 0; this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(startIdentity, startIdentity === COMPAS_PRODUCT ? "Compas" : "FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const ports of [this.access.inputs, this.access.outputs]) {
      for (const port of ports.values()) port.state = "disconnected";
      ports.clear();
    }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (data) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(data)), 1);
    }};
    this.access.inputs.set(this.input.id, this.input); this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) {
    const input = this.input;
    setTimeout(() => {
      if (input.state === "connected" && input.onmidimessage) input.onmidimessage({ data: Uint8Array.from(bytes) });
    }, 1);
  }
  ident(text) {
    const raw = [...new TextEncoder().encode(text), ...new Array(27 - text.length).fill(0)];
    const checksum = ~raw.reduce((sum, value) => sum + value, 0) & 0xFF;
    this.tx([0xF0, ...pack7([0, 0x59, 0x11, 27, 0, 0, ...raw, checksum]), 0xF7]);
  }
  rx(data) {
    if (eq(data, HS)) {
      this.ident(this.extendedHandshake && this.identity.startsWith("FM-1_922") ? "FM-1_922" : this.identity);
      return;
    }
    if (eq(data, HS_FULL)) {
      this.fullQueries++;
      if (this.identity.startsWith("FM-1_922")) this.bb1FullQueries++;
      if (this.extendedHandshake && this.identity.startsWith("FM-1_922")) this.ident(this.identity);
      return;
    }
    if (eq(data, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next(); return;
    }
    if (!this.waiting) return;
    const unpacked = unpack7(data.slice(1, -1));
    const [addr, len] = this.waiting;
    const got = unpacked.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
    const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0]
      : Array.from(this.image.subarray(addr, addr + len));
    if (!eq(got, want)) this.bad++;
    this.waiting = null; this.served++;
    if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
    if (addr === 0xE0000000) setTimeout(() => this.boot("ota-" + this.targetIdentity, "FM-1 Update"), 300);
    else if (addr === 0xF0000000) setTimeout(() => this.boot(this.targetIdentity, this.targetIdentity === COMPAS_PRODUCT ? "Compas" : "FM-1"), 300);
    else this.next();
  }
  next() {
    const request = this.queue.shift(); if (!request) return;
    this.waiting = request;
    const [addr, len] = request;
    const unpacked = [0, 0x59, 0x30, 0, 0, 0, 0,
      addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
      len & 0xFF, len >> 8, 0];
    let sum = 0; for (let i = 6; i < 14; i++) sum += unpacked[i];
    unpacked.push(~sum & 0xFF); this.tx([0xF0, ...pack7(unpacked), 0xF7]);
  }
}


const simulated = [];
async function run(start, target) {
  const device = new FakeFM1(target.image, {
    startIdentity:start, targetIdentity:target.product, extendedHandshake:true,
  });
  const steps = [];
  const result = await new Updater(device.access).install(target.image, target.product,
    (step) => steps.push(step), STARTING_PRODUCTS);
  assert.equal(result, target.product);
  assert.equal(device.bad, 0);
  assert.equal(steps.at(-1), "done");
  assert(steps.includes("loader") && steps.includes("write"));
  if (start !== "FM-1_015") assert(device.bb1FullQueries > 0);
  simulated.push({start, target:target.product, served:device.served});
}

await Promise.all([...STARTING_PRODUCTS.map(start => run(start, compas)), run(COMPAS_PRODUCT, stock)]);

const paused = new FakeFM1(compas.image, {
  startIdentity:COMPAS_PRODUCT, targetIdentity:COMPAS_PRODUCT, extendedHandshake:true,
  unplugAfter:6,
});
await assert.rejects(new Updater(paused.access).install(compas.image, compas.product, null, STARTING_PRODUCTS),
  error => error.code === "lost");
paused.unplugAfter = Infinity;
paused.boot("ota-" + COMPAS_PRODUCT, "FM-1 Update");
assert(await new Updater(paused.access).resume(compas.image, null, compas.product));
assert.equal(paused.bad, 0);

const unsupported = new FakeFM1(compas.image, {
  startIdentity:"FM-1_922", targetIdentity:COMPAS_PRODUCT,
});
await assert.rejects(new Updater(unsupported.access).install(compas.image, compas.product, null, STARTING_PRODUCTS),
  error => error.code === "baseline");
assert.equal(unsupported.served, 0);
console.log(JSON.stringify({scope:"mock Web MIDI only", simulated, interruptedLoaderResumed:true,
  unrecognizedBaselineRefused:true, mismatchedPackageRefused:true}, null, 2));
