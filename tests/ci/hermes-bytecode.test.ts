import { describe, expect, it } from "bun:test";
import { spawn, spawnSync } from "bun";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

const MYSTRAL_BIN = join(import.meta.dir, "../../build/mystral");
const hasHermes = existsSync(MYSTRAL_BIN) && spawnSync({
  cmd: [MYSTRAL_BIN, "--version"], stdout: "pipe", stderr: "pipe",
}).stdout.toString().includes("hermes build");

async function run(...args: string[]) {
  const proc = spawn({ cmd: [MYSTRAL_BIN, ...args], stdout: "pipe", stderr: "pipe" });
  const [stdout, stderr, exitCode] = await Promise.all([
    new Response(proc.stdout).text(), new Response(proc.stderr).text(), proc.exited,
  ]);
  return { stdout, stderr, exitCode };
}

describe.skipIf(!existsSync(MYSTRAL_BIN))("Hermes bytecode compiler", () => {
  it("compiles source when enabled and reports unavailable support otherwise", async () => {
    const directory = mkdtempSync(join(tmpdir(), "mystral-hermes-"));
    try {
      const source = join(directory, "entry.js");
      const output = join(directory, "nested", "entry.hbc");
      writeFileSync(source, "console.log('compiled');");
      const result = await run("compile", source, "--hermes-bytecode", "--out", output);
      if (hasHermes) {
        expect(result.exitCode).toBe(0);
        expect(result.stdout).toContain("Compiled Hermes bytecode:");
        expect(readFileSync(output).length).toBeGreaterThan(0);
      } else {
        expect(result.exitCode).toBe(1);
        expect(result.stderr).toContain("MYSTRAL_USE_HERMES=ON");
        expect(existsSync(output)).toBe(false);
      }
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
  });

  it.skipIf(!hasHermes)("rejects invalid source without creating bytecode", async () => {
    const directory = mkdtempSync(join(tmpdir(), "mystral-hermes-"));
    try {
      const source = join(directory, "invalid.js");
      const output = join(directory, "invalid.hbc");
      writeFileSync(source, "function {");
      const result = await run("compile", source, "--hermes-bytecode");
      expect(result.exitCode).toBe(1);
      expect(result.stderr).toContain("error");
      expect(existsSync(output)).toBe(false);
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
  });

  it.skipIf(!hasHermes)("uses the entry name for the default bytecode output", async () => {
    const directory = mkdtempSync(join(tmpdir(), "mystral-hermes-"));
    try {
      const source = join(directory, "entry.js");
      writeFileSync(source, "console.log('default output');");
      expect((await run("compile", source, "--hermes-bytecode")).exitCode).toBe(0);
      expect(readFileSync(join(directory, "entry.hbc")).length).toBeGreaterThan(0);
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
  });
});
