/**
 * Durable on-Neon Whisper STT (no external API keys).
 * Vendors @xenova/transformers + onnxruntime-node into /tmp on first use.
 * Marker: pocket-whisper-runtime-v1
 */
import { mkdirSync, writeFileSync, existsSync } from "fs";
import { execSync } from "child_process";
import { join } from "path";
import { tmpdir } from "os";
import { createRequire } from "module";

const VENDOR = join(tmpdir(), "pocket-whisper-vendor-v1");
const MARKER = join(VENDOR, ".ready");
let pipelinePromise = null;

async function ensureVendor() {
  if (existsSync(MARKER)) return;
  mkdirSync(join(VENDOR, "node_modules"), { recursive: true });
  // Install with npm into vendor dir (Neon Functions include npm).
  execSync("npm install --omit=dev --no-audit --no-fund @xenova/transformers@2.17.2 onnxruntime-node@1.14.0", {
    cwd: VENDOR,
    env: { ...process.env, npm_config_cache: join(VENDOR, ".npm-cache"), NODE_ENV: "production" },
    stdio: "pipe",
    timeout: 180000,
  });
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
    return pipeline("automatic-speech-recognition", "Xenova/whisper-tiny.en");
  })();
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
    const u = new URL(r.url);
    if (r.method === "GET") {
      return Response.json(
        { ok: true, service: "pocket-whisper-runtime-v1", vendor: existsSync(MARKER) },
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
