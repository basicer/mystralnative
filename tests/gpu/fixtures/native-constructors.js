(async () => {
    function check(condition, message) {
        if (!condition) throw new Error(message);
    }
    check(AudioContext.name === 'AudioContext', 'Constructor name');
    const first = new AudioContext();
    const second = Reflect.construct(AudioContext, []);
    check(first !== second, 'Independent instances');
    check(typeof first.createBuffer === 'function', 'Native object returned');
    const buffer = first.createBuffer(2, 16, 22050);
    check(buffer.numberOfChannels === 2 && buffer.length === 16, 'Native arguments');
    check(buffer.sampleRate === 22050, 'Sample rate argument');
    check(typeof second.createBufferSource === 'function', 'Reflect.construct');
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
    console.log('NATIVE_CONSTRUCTORS_PASS');
})().catch(error => console.error('NATIVE_CONSTRUCTORS_FAIL', String(error)));
