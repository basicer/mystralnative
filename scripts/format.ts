#!/usr/bin/env bun
import { Glob, spawnSync, which } from "bun";
import { lstatSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "fs";
import { dirname, join } from "path";
import { tmpdir } from "os";

const args = process.argv.slice(2);
const check = args.includes("--check");
const diff = args.includes("--diff");
const all = args.includes("--all");
let base = "HEAD";
for (let i = 0; i < args.length; i++) {
  if (args[i] === "--base" && args[i + 1]) {
    base = args[++i];
  } else if (!["--check", "--diff", "--all"].includes(args[i])) {
    console.error("Usage: bun run format [--check | --diff] [--all | --base <revision>]");
    process.exit(2);
  }
}
if (check && diff) {
  console.error("Use either --check or --diff, not both.");
  process.exit(2);
}
if (all && args.includes("--base")) {
  console.error("Use either --all or --base, not both.");
  process.exit(2);
}

function run(cmd: string[], input?: string) {
  const result = spawnSync({
    cmd,
    stdin: input === undefined ? "ignore" : Buffer.from(input),
    stdout: "pipe",
    stderr: "inherit",
  });
  if (result.exitCode !== 0) process.exit(result.exitCode || 2);
  return result.stdout.toString();
}

function runDiff(file: string, formattedFile: string) {
  const result = spawnSync({
    cmd: ["diff", "-u", "--label", `a/${file}`, "--label", `b/${file}`, file, formattedFile],
    stdout: "pipe",
    stderr: "inherit",
  });
  if (result.exitCode !== 0 && result.exitCode !== 1) process.exit(result.exitCode || 2);
  return result.stdout.toString();
}

process.chdir(run(["git", "rev-parse", "--show-toplevel"]).trim());
const formatter = which(process.env.CLANG_FORMAT || "clang-format");
if (!formatter || !/\bversion 18\./.test(run([formatter, "--version"]))) {
  console.error("clang-format 18 is required. Set CLANG_FORMAT to its executable path.");
  process.exit(2);
}
if (!all) base = run(["git", "rev-parse", "--verify", "--end-of-options", `${base}^{commit}`]).trim();

const ignored = readFileSync(".clang-format-ignore", "utf8")
  .split(/\r?\n/)
  .map(line => line.trim())
  .filter(line => line && !line.startsWith("#"))
  .map(pattern => new Glob(pattern));
const files = run(["git", "ls-files", "-z"])
  .split("\0")
  .filter(file => /\.(c|cc|cpp|cxx|h|hh|hpp|hxx|m|mm)$/.test(file))
  .filter(file => !ignored.some(pattern => pattern.match(file)))
  .filter(file => lstatSync(file, { throwIfNoEntry: false })?.isFile());

let changed = 0;
const previewDirectory = diff ? mkdtempSync(join(tmpdir(), "mystral-format-")) : null;
try {
  for (const file of files) {
    const ranges: string[] = [];
    if (!all) {
      const changedLines = run(["git", "diff", "--no-ext-diff", "--no-textconv", "--no-color", "--unified=0", base, "--", file]);
      for (const match of changedLines.matchAll(/^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@/gm)) {
        const start = Math.max(1, Number(match[1]));
        const count = Number(match[2] ?? 1);
        ranges.push(`--lines=${start}:${start + Math.max(1, count) - 1}`);
      }
      if (!ranges.length) continue;
    }
    const original = readFileSync(file, "utf8");
    const formatted = run([
      formatter, "--style=file", "--Werror", `--assume-filename=${join(process.cwd(), file)}`, ...ranges,
    ], original);
    if (formatted === original) continue;
    changed++;
    if (diff) {
      const formattedFile = join(previewDirectory!, file);
      mkdirSync(dirname(formattedFile), { recursive: true });
      writeFileSync(formattedFile, formatted);
      process.stdout.write(runDiff(file, formattedFile));
    } else {
      console.log(`${check ? "Needs formatting" : "Formatted"}: ${file}`);
      if (!check) writeFileSync(file, formatted);
    }
  }
} finally {
  if (previewDirectory) rmSync(previewDirectory, { recursive: true, force: true });
}
if (!diff) console.log(`${changed} file(s) ${check ? "need formatting" : "formatted"}.`);
process.exit(check && changed ? 1 : 0);
