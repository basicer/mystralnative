import { describe, expect, it } from "bun:test";
import { spawn } from "bun";
import { existsSync, mkdtempSync, rmSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";

const MYSTRAL_BIN = join(import.meta.dir, "../../build/mystral");
const FIXTURE = join(import.meta.dir, "fixtures/native-constructors.js");

describe.skipIf(!existsSync(MYSTRAL_BIN))("Native constructors", () => {
  for (const mode of ["--headless"]) {
    it(`runs successfully (${mode})`, async () => {
      const output = mkdtempSync(join(tmpdir(), "mystral-native-constructors-"));
      const proc = spawn({
        cmd: [MYSTRAL_BIN, "run", FIXTURE, mode,
          "--screenshot", join(output, "canvas.png"), "--frames", "10"],
        stdout: "pipe",
        stderr: "pipe",
        env: { ...process.env, SDL_AUDIO_DRIVER: "dummy" },
      });
      try {
        const [stdout, stderr, exitCode] = await Promise.all([
          new Response(proc.stdout).text(), new Response(proc.stderr).text(), proc.exited,
        ]);
        expect(exitCode).toBe(0);
        expect(stdout).toContain("NATIVE_CONSTRUCTORS_PASS");
        expect(stdout + stderr).not.toContain("NATIVE_CONSTRUCTORS_FAIL");
        expect(stderr).not.toContain("Device error");
      } finally {
        proc.kill();
        await proc.exited;
        rmSync(output, { recursive: true, force: true });
      }
    }, 30000);
  }
});
