import { describe, expect, it } from "bun:test";
import { spawn } from "bun";
import { existsSync, mkdtempSync, rmSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

const MYSTRAL_BIN = join(import.meta.dir, "../../build/mystral");
const FIXTURE = join(import.meta.dir, "fixtures/texture-copy-stride.js");

describe.skipIf(!existsSync(MYSTRAL_BIN))("Optional texture copy strides", () => {
  for (const mode of ["--headless", "--no-sdl"]) {
    it(`runs successfully (${mode})`, async () => {
      const output = mkdtempSync(join(tmpdir(), "mystral-texture-copy-stride-"));
      const proc = spawn({
        cmd: [MYSTRAL_BIN, "run", FIXTURE, mode,
          "--screenshot", join(output, "canvas.png"), "--frames", "60"],
        stdout: "pipe",
        stderr: "pipe",
      });
      try {
        const [stdout, stderr, exitCode] = await Promise.all([
          new Response(proc.stdout).text(), new Response(proc.stderr).text(), proc.exited,
        ]);
        expect(exitCode).toBe(0);
        expect(stdout).toContain("TEXTURE_COPY_STRIDE_PASS");
        expect(stdout + stderr).not.toContain("TEXTURE_COPY_STRIDE_FAIL");
        expect(stderr).not.toContain("Device error");
      } finally {
        proc.kill();
        await proc.exited;
        rmSync(output, { recursive: true, force: true });
      }
    }, 30000);
  }
});
