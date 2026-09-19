import { describe, expect, it } from "bun:test";
import { spawn, spawnSync } from "bun";
import { existsSync, mkdtempSync, rmSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

const MYSTRAL_BIN = join(import.meta.dir, "../../build/mystral");
const hasHermes = existsSync(MYSTRAL_BIN) && spawnSync({
  cmd: [MYSTRAL_BIN, "--version"], stdout: "pipe", stderr: "pipe",
}).stdout.toString().includes("hermes build");

async function run(...args: string[]) {
  const proc = spawn({ cmd: [MYSTRAL_BIN, ...args], stdout: "pipe", stderr: "pipe" });
  const timer = setTimeout(() => proc.kill(), 20000);
  try {
    const [stdout, stderr, exitCode] = await Promise.all([
      new Response(proc.stdout).text(), new Response(proc.stderr).text(), proc.exited,
    ]);
    return { stdout, stderr, exitCode };
  } finally {
    clearTimeout(timer);
    proc.kill();
    await proc.exited;
  }
}

describe.skipIf(!existsSync(MYSTRAL_BIN))("Hermes bytecode runtime", () => {
  it.skipIf(!hasHermes)("runs compiled code and drains Promise continuations", async () => {
    const directory = mkdtempSync(join(tmpdir(), "mystral-hermes-"));
    try {
      const source = join(directory, "entry.js");
      const bytecode = join(directory, "entry.hbc");
      writeFileSync(source, `
        (async () => {
          const value = await Promise.resolve(42);
          if (value !== 42) throw new Error('Promise value lost');
    const device = await (await navigator.gpu.requestAdapter()).requestDevice();
    const context = canvas.getContext('webgpu');
    context.configure({ device, format: navigator.gpu.getPreferredCanvasFormat() });
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({ colorAttachments: [{
        view: context.getCurrentTexture().createView(),
        clearValue: { r: 0, g: 1, b: 0, a: 1 }, loadOp: 'clear', storeOp: 'store',
    }] });
    pass.end();
    device.queue.submit([encoder.finish()]);
          console.log('HERMES_BYTECODE_PASS');
        })().catch(error => console.error('HERMES_BYTECODE_FAIL', String(error)));
      `);
      expect((await run("compile", source, "--hermes-bytecode")).exitCode).toBe(0);
      const result = await run("run", bytecode, "--no-sdl", "--screenshot", join(directory, "canvas.png"), "--frames", "60");
      expect(result.exitCode).toBe(0);
      expect(result.stdout).toContain("HERMES_BYTECODE_PASS");
      expect(result.stdout + result.stderr).not.toContain("HERMES_BYTECODE_FAIL");
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
  }, 30000);

  it("rejects invalid bytecode or an incompatible runtime", async () => {
    const directory = mkdtempSync(join(tmpdir(), "mystral-hermes-"));
    try {
      const bytecode = join(directory, "invalid.hbc");
      writeFileSync(bytecode, "not Hermes bytecode");
      const result = await run("run", bytecode, "--no-sdl");
      expect(result.exitCode).toBe(1);
      expect(result.stderr).toContain(hasHermes ? "Invalid Hermes bytecode" : "Hermes bytecode requires a Hermes build");
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
  }, 30000);
});
