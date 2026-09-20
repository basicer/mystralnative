#!/usr/bin/env node

/**
 * Give a prebuilt Dawn static archive a private Abseil ABI namespace.
 *
 * This is for linking Dawn together with another static dependency (such as
 * V8) that ships an incompatible copy of Abseil.  It rewrites C++ mangled
 * symbols such as `__ZN4absl...` to `__ZN9dawn_absl...`, including relocations
 * between object files in the archive.
 *
 * The original archive is never changed.
 *
 * Usage:
 *   node scripts/seal-dawn-absl.mjs third_party/dawn/lib/libwebgpu_dawn.a \
 *     --output third_party/dawn/lib/libwebgpu_dawn_sealed.a
 *
 * Prerequisites:
 *   LLVM's llvm-objcopy and an nm-compatible symbol lister. Set LLVM_OBJCOPY
 *   and LLVM_NM to absolute paths when they are not available on PATH.
 *
 * The output is architecture-specific. Run separately for each per-architecture
 * archive; do not give this script a universal/fat archive. macOS, Linux, and
 * Windows COFF static libraries are supported.
 */

import { execFileSync } from 'node:child_process';
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  renameSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { basename, dirname, extname, join, resolve } from 'node:path';
import { tmpdir } from 'node:os';

const OLD_NAMESPACE_COMPONENT = '4absl';
const NEW_NAMESPACE_COMPONENT = '9dawn_absl';

function usage(exitCode = 0) {
  const out = `Usage: node scripts/seal-dawn-absl.mjs <input.a|input.lib> [options]

Options:
  -o, --output <path>  Destination archive (default: <input>.sealed.<ext>)
  --force              Replace an existing destination archive
  --keep-temp          Keep extracted objects and the symbol map for inspection
  -h, --help           Show this help
`;
  (exitCode === 0 ? process.stdout : process.stderr).write(out);
  process.exit(exitCode);
}

function fail(message) {
  process.stderr.write(`error: ${message}\n`);
  process.exit(1);
}

function run(command, args, options = {}) {
  try {
    return execFileSync(command, args, {
      cwd: options.cwd,
      encoding: 'utf8',
      stdio: options.stdio ?? ['ignore', 'pipe', 'pipe'],
    });
  } catch (error) {
    const stderr = error.stderr?.toString().trim();
    fail(`${command} ${args.join(' ')} failed${stderr ? `:\n${stderr}` : ''}`);
  }
}

function findTool(environmentVariable, fallback) {
  return process.env[environmentVariable] || fallback;
}

function parseArgs(argv) {
  const result = { force: false, keepTemp: false, input: null, output: null };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === '-h' || argument === '--help') usage();
    if (argument === '--force') {
      result.force = true;
    } else if (argument === '--keep-temp') {
      result.keepTemp = true;
    } else if (argument === '-o' || argument === '--output') {
      result.output = argv[++index];
      if (!result.output) fail(`${argument} needs a path`);
    } else if (argument.startsWith('-')) {
      fail(`unknown option: ${argument}`);
    } else if (!result.input) {
      result.input = argument;
    } else {
      fail(`unexpected argument: ${argument}`);
    }
  }
  if (!result.input) usage(1);
  return result;
}

function symbolFromNmLine(line) {
  // llvm-nm prints address/type columns followed by one Mach-O symbol. All
  // symbols this tool changes are Itanium C++ symbols, so whitespace is not
  // valid within their names.
  return line.trim().split(/\s+/).at(-1);
}

function isAbseilCppSymbol(symbol) {
  // The first underscore is the Mach-O external-symbol prefix; the remaining
  // `_Z` begins an Itanium C++ mangled name. MSVC represents a namespace as
  // `@absl@@`. `4absl` can occur repeatedly in a template signature.
  return (
    (/^_+Z/.test(symbol) && symbol.includes(OLD_NAMESPACE_COMPONENT)) ||
    (symbol.startsWith('?') && symbol.includes('@absl@'))
  );
}

function renamedSymbol(symbol) {
  if (/^_+Z/.test(symbol)) {
    return symbol.split(OLD_NAMESPACE_COMPONENT).join(NEW_NAMESPACE_COMPONENT);
  }
  return symbol.split('@absl@').join('@dawn_absl@');
}

function archiveObjects(archivePath, destinationDirectory) {
  // `ar -x` overwrites repeated member names. Dawn archives commonly contain
  // several `escaping.cc.o`-style names, so parse the BSD/GNU ar container
  // ourselves and give each extracted member a unique, ordinal-prefixed name.
  const archive = readFileSync(archivePath);
  const magic = archive.subarray(0, 8).toString('ascii');
  if (magic !== '!<arch>\n') {
    fail('input is not a regular ar archive (fat/universal archives are unsupported)');
  }

  const objectPaths = [];
  let gnuLongNameTable = null;
  let offset = 8;
  let ordinal = 0;
  while (offset < archive.length) {
    if (offset + 60 > archive.length) fail('truncated ar member header');
    const header = archive.subarray(offset, offset + 60);
    if (header.subarray(58, 60).toString('ascii') !== '`\n') fail('invalid ar member header');

    const memberSize = Number.parseInt(header.subarray(48, 58).toString('ascii').trim(), 10);
    if (!Number.isSafeInteger(memberSize) || memberSize < 0) fail('invalid ar member size');
    const memberStart = offset + 60;
    const memberEnd = memberStart + memberSize;
    if (memberEnd > archive.length) fail('truncated ar member payload');

    let memberName = header.subarray(0, 16).toString('ascii').trim();
    let payloadStart = memberStart;
    if (memberName.startsWith('#1/')) {
      const nameLength = Number.parseInt(memberName.slice(3), 10);
      if (!Number.isSafeInteger(nameLength) || nameLength < 0 || nameLength > memberSize) {
        fail('invalid BSD extended ar member name');
      }
      memberName = archive.subarray(memberStart, memberStart + nameLength).toString('utf8').replace(/\0+$/, '');
      payloadStart += nameLength;
    } else if (memberName === '//') {
      // GNU ar stores long names once in this member; individual entries use
      // `/offset` into the table and end in `/\\n`.
      gnuLongNameTable = archive.subarray(memberStart, memberEnd).toString('utf8');
    } else if (/^\/\d+$/.test(memberName)) {
      if (gnuLongNameTable === null) fail('GNU ar member references a missing long-name table');
      const nameOffset = Number.parseInt(memberName.slice(1), 10);
      const nameEnd = gnuLongNameTable.indexOf('/\n', nameOffset);
      if (!Number.isSafeInteger(nameOffset) || nameOffset < 0 || nameEnd === -1) {
        fail('invalid GNU ar long member name');
      }
      memberName = gnuLongNameTable.slice(nameOffset, nameEnd);
    } else if (memberName.endsWith('/')) {
      memberName = memberName.slice(0, -1);
    }

    // `__.SYMDEF*`, `/`, and `//` are archive metadata, not object members.
    if (memberName.endsWith('.o') || memberName.endsWith('.obj')) {
      const safeName = memberName.split(/[\\/]/).at(-1).replace(/[^A-Za-z0-9._-]/g, '_');
      const objectPath = join(destinationDirectory, `${String(ordinal).padStart(6, '0')}-${safeName}`);
      writeFileSync(objectPath, archive.subarray(payloadStart, memberEnd));
      objectPaths.push(objectPath);
      ordinal += 1;
    }
    offset = memberEnd + (memberSize % 2);
  }
  return objectPaths;
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  const input = resolve(options.input);
  if (!existsSync(input)) fail(`input archive does not exist: ${input}`);
  const inputExtension = extname(input).toLowerCase();
  const supportedExtension = process.platform === 'win32'
    ? inputExtension === '.lib'
    : inputExtension === '.a';
  if (!supportedExtension) {
    fail(`input must be a static ${process.platform === 'win32' ? '.lib' : '.a'} archive: ${input}`);
  }

  const defaultOutput = join(dirname(input), `${basename(input, inputExtension)}.sealed${inputExtension}`);
  const output = resolve(options.output ?? defaultOutput);
  if (input === output) fail('output must differ from input; the source archive is preserved');
  if (existsSync(output) && !options.force) {
    fail(`output already exists: ${output} (pass --force to replace it)`);
  }
  mkdirSync(dirname(output), { recursive: true });

  const nm = findTool('LLVM_NM', process.platform === 'win32' ? 'llvm-nm' : 'nm');
  const objcopy = findTool('LLVM_OBJCOPY', 'llvm-objcopy');
  const ar = process.platform === 'linux' ? findTool('AR', 'ar') : null;
  const libtool = process.platform === 'darwin' ? findTool('LIBTOOL', 'libtool') : null;
  const windowsLib = process.platform === 'win32' ? (process.env.LLVM_LIB || 'llvm-lib') : null;
  const nmArguments = process.platform === 'darwin' ? ['-gU'] : ['-g'];

  const workspace = mkdtempSync(join(tmpdir(), 'mystral-seal-dawn-absl-'));
  const objectsDirectory = join(workspace, 'objects');
  const mapPath = join(workspace, 'absl-symbol-map.txt');
  const replacementPath = join(workspace, process.platform === 'win32' ? 'replacement.lib' : 'replacement.a');

  try {
    mkdirSync(objectsDirectory);
    const objects = archiveObjects(input, objectsDirectory);
    if (objects.length === 0) fail('archive contains no object files');

    const symbols = new Set();
    for (const objectPath of objects) {
      const outputText = run(nm, [...nmArguments, objectPath]);
      for (const line of outputText.split(/\r?\n/)) {
        const symbol = symbolFromNmLine(line);
        if (symbol && isAbseilCppSymbol(symbol)) symbols.add(symbol);
      }
    }
    if (symbols.size === 0) {
      // This is normal when the archive is already the output of this script.
      // Keep the build rule idempotent so a pre-sealed vendor archive can still
      // be copied to CMake's generated dependency location.
      if (existsSync(output)) rmSync(output);
      copyFileSync(input, output);
      process.stdout.write(`Dawn archive already has no external C++ Abseil symbols; copied without changes:\n  ${input}\n  → ${output}\n`);
      return;
    }

    const mapLines = [...symbols].sort().map((symbol) => `${symbol} ${renamedSymbol(symbol)}`);
    writeFileSync(mapPath, `${mapLines.join('\n')}\n`);

    for (const objectPath of objects) {
      const rewrittenObject = `${objectPath}.sealed`;
      run(objcopy, [`--redefine-syms=${mapPath}`, objectPath, rewrittenObject]);
      renameSync(rewrittenObject, objectPath);
    }

    // Apple libtool reliably writes a Mach-O ranlib index. GNU/LLVM ar's `s`
    // modifier does the same for ELF archives.
    if (process.platform === 'darwin') {
      run(libtool, ['-static', '-o', replacementPath, ...objects]);
    } else if (process.platform === 'linux') {
      run(ar, ['rcs', replacementPath, ...objects]);
    } else if (process.platform === 'win32') {
      // llvm-lib emits a COFF .lib accepted by both link.exe and lld-link.
      run(windowsLib, [`/OUT:${replacementPath}`, ...objects]);
    } else {
      fail(`unsupported platform: ${process.platform}`);
    }

    // Inspect the objects rather than the rebuilt archive: Apple llvm-nm
    // returns a non-zero status for some valid archive symbol-table variants.
    const remaining = objects.flatMap((objectPath) => run(nm, [...nmArguments, objectPath])
      .split(/\r?\n/)
      .map(symbolFromNmLine)
      .filter(isAbseilCppSymbol));
    if (remaining.length > 0) {
      fail(`rewritten archive still exports ${remaining.length} absl symbol(s); refusing to emit unsafe output`);
    }

    // Use rename rather than copying a partially-built archive into place.
    if (existsSync(output)) rmSync(output);
    renameSync(replacementPath, output);
    process.stdout.write(`Sealed ${symbols.size} Abseil C++ symbols:\n  ${input}\n  → ${output}\n`);
    process.stdout.write(`Symbol map: ${options.keepTemp ? mapPath : '(removed)'}\n`);
  } finally {
    if (!options.keepTemp) rmSync(workspace, { recursive: true, force: true });
    else process.stdout.write(`Temporary files kept at: ${workspace}\n`);
  }
}

main();
