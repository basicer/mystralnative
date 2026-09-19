(async () => {
    const audio = new AudioContext();
    const buffer = audio.createBuffer(1, 64, audio.sampleRate);
    buffer.getChannelData(0).fill(0.01);
    const source = audio.createBufferSource();
    source.buffer = buffer;
    if (source.buffer !== buffer) throw new Error('Buffer identity lost');
    source.buffer = null;
    if (source.buffer !== null) throw new Error('Buffer reset failed');
    source.buffer = buffer;
    source.connect(audio.destination);
    source.start();
    const loop = audio.createBufferSource();
    loop.buffer = buffer;
    loop.start();
    loop.loop = true; // ZzFX sets this after start().
    loop.loopStart = 8 / audio.sampleRate;
    loop.loopEnd = 32 / audio.sampleRate;
    if (!loop.loop || loop.loopStart !== 8 / audio.sampleRate) throw new Error('Loop properties failed');
    audio.resume();
    await new Promise(resolve => setTimeout(resolve, 200));
    const firstTime = audio._getCurrentTime();
    if (firstTime <= 0) throw new Error('Audio callback did not advance');
    loop.loop = false;
    await new Promise(resolve => setTimeout(resolve, 200));
    if (audio._getCurrentTime() <= firstTime) throw new Error('Mixer froze after source ended');
    audio.suspend();
    const device = await (await navigator.gpu.requestAdapter()).requestDevice();
    const ctx = canvas.getContext('webgpu');
    ctx.configure({ device, format: navigator.gpu.getPreferredCanvasFormat() });
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({ colorAttachments: [{
        view: ctx.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store',
        clearValue: { r: 0, g: 1, b: 0, a: 1 },
    }] });
    pass.end();
    device.queue.submit([encoder.finish()]);
    console.log('AUDIO_SOURCE_PASS');
})().catch(error => console.error('AUDIO_SOURCE_FAIL', String(error)));
