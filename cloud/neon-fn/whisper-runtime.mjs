/**
 * Durable on-Neon Whisper STT (no external API keys).
 * Downloads a prebuilt wasm vendor+model tarball (fits Neon /tmp 256MB).
 * Marker: pocket-whisper-runtime-v2
 *
 * Vendor release: github.com/bighappysmiley/Pocket/releases/tag/whisper-vendor-v2
 */
import { mkdirSync, writeFileSync, existsSync, rmSync, createWriteStream } from "fs";
import { execSync } from "child_process";
import { join } from "path";
import { tmpdir } from "os";
import { createRequire } from "module";
import { pipeline as streamPipeline } from "stream/promises";
import { Readable } from "stream";

const VENDOR = join(tmpdir(), "pocket-whisper-vendor-v2");
const MARKER = join(VENDOR, ".ready");
const VENDOR_URL =
  "https://github.com/bighappysmiley/Pocket/releases/download/whisper-vendor-v2/pocket-whisper-vendor-v2.tgz";
let pipelinePromise = null;

const PATH_ENV = ["/usr/local/bin", "/usr/bin", process.env.PATH || ""].filter(Boolean).join(":");

function run(cmd, opts = {}) {
  return execSync(cmd, {
    encoding: "utf8",
    env: { ...process.env, PATH: PATH_ENV, ...(opts.env || {}) },
    timeout: opts.timeout || 120000,
    stdio: opts.stdio || "pipe",
    cwd: opts.cwd,
  });
}

async function downloadTo(url, dest) {
  const res = await fetch(url, { redirect: "follow" });
  if (!res.ok) throw new Error(`vendor download ${res.status}`);
  if (!res.body) throw new Error("vendor download empty body");
  const nodeStream = Readable.fromWeb(res.body);
  await streamPipeline(nodeStream, createWriteStream(dest));
}

async function ensureVendor() {
  if (existsSync(MARKER)) return;
  // Drop prior failed npm installs / old vendor dirs so /tmp stays under budget.
  for (const name of ["pocket-whisper-vendor-v1", "pocket-whisper-vendor-v2", "pocket-whisper-vendor-v2.tgz"]) {
    try {
      rmSync(join(tmpdir(), name), { recursive: true, force: true });
    } catch {
      /* ignore */
    }
  }
  mkdirSync(VENDOR, { recursive: true });
  const tgz = join(tmpdir(), "pocket-whisper-vendor-v2-dl.tgz");
  try {
    await downloadTo(VENDOR_URL, tgz);
    run(`tar -xzf ${JSON.stringify(tgz)} -C ${JSON.stringify(VENDOR)}`, { timeout: 180000 });
  } finally {
    try {
      rmSync(tgz, { force: true });
    } catch {
      /* ignore */
    }
  }
  if (!existsSync(join(VENDOR, "package.json")) || !existsSync(join(VENDOR, "node_modules"))) {
    throw new Error("vendor tarball missing package.json/node_modules");
  }
  writeFileSync(MARKER, "ok");
}

async function getPipeline() {
  if (pipelinePromise) return pipelinePromise;
  pipelinePromise = (async () => {
    await ensureVendor();
    const req = createRequire(join(VENDOR, "package.json"));
    const mod = req("@xenova/transformers");
    const { pipeline, env } = mod;
    env.allowLocalModels = false;
    env.useBrowserCache = false;
    env.cacheDir = join(VENDOR, "model-cache");
    if (env.backends?.onnx?.wasm) {
      env.backends.onnx.wasm.numThreads = 1;
      env.backends.onnx.wasm.proxy = false;
    }
    return pipeline("automatic-speech-recognition", "Xenova/whisper-tiny.en", {
      quantized: true,
    });
  })().catch((e) => {
    pipelinePromise = null;
    throw e;
  });
  return pipelinePromise;
}

function pcmOrWavToFloat32(buf, headerRate) {
  if (buf.length > 44 && buf.toString("ascii", 0, 4) === "RIFF") {
    let offset = 12;
    let dataOffset = 44;
    let sampleRate = 16000;
    let bits = 16;
    let ch = 1;
    while (offset + 8 <= buf.length) {
      const id = buf.toString("ascii", offset, offset + 4);
      const size = buf.readUInt32LE(offset + 4);
      if (id === "fmt ") {
        ch = buf.readUInt16LE(offset + 10);
        sampleRate = buf.readUInt32LE(offset + 12);
        bits = buf.readUInt16LE(offset + 22);
      } else if (id === "data") {
        dataOffset = offset + 8;
        const n = Math.floor((bits === 16 ? size / 2 : size) / ch);
        const out = new Float32Array(n);
        for (let i = 0, j = 0; i < n; i++, j += (bits / 8) * ch) {
          out[i] = buf.readInt16LE(dataOffset + j) / 32768;
        }
        return { audio: out, sampleRate };
      }
      offset += 8 + size + (size % 2);
    }
  }
  const n = Math.floor(buf.length / 2);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = buf.readInt16LE(i * 2) / 32768;
  return { audio: out, sampleRate: headerRate || 16000 };
}

function cors() {
  return {
    "access-control-allow-origin": "*",
    "access-control-allow-headers": "*",
    "access-control-allow-methods": "GET,POST,OPTIONS",
    "content-type": "application/json",
  };
}

export default {
  async fetch(r) {
    if (r.method === "OPTIONS") return new Response(null, { status: 204, headers: cors() });
    if (r.method === "GET") {
      return Response.json(
        {
          ok: true,
          service: "pocket-whisper-runtime-v2",
          vendor: existsSync(MARKER),
          vendorUrl: VENDOR_URL,
        },
        { headers: cors() },
      );
    }
    if (r.method !== "POST") {
      return Response.json({ message: "POST audio" }, { status: 405, headers: cors() });
    }
    try {
      const ab = await r.arrayBuffer();
      const buf = Buffer.from(ab);
      if (buf.length < 320) return Response.json({ message: "empty" }, { status: 400, headers: cors() });
      const rate = Math.max(8000, Math.min(48000, parseInt(r.headers.get("x-pcm-rate") || "16000", 10) || 16000));
      const { audio, sampleRate } = pcmOrWavToFloat32(buf, rate);
      const transcriber = await getPipeline();
      const result = await transcriber(audio, {
        sampling_rate: sampleRate,
        chunk_length_s: 30,
        stride_length_s: 5,
      });
      const text = typeof result?.text === "string" ? result.text.trim() : "";
      if (!text) return Response.json({ message: "empty transcript" }, { status: 502, headers: cors() });
      return Response.json({ text }, { headers: cors() });
    } catch (e) {
      return Response.json({ message: String(e && e.message || e) }, { status: 502, headers: cors() });
    }
  },
};
