(async () => {
    const device = await (await navigator.gpu.requestAdapter()).requestDevice();
    const expected = new Uint32Array([123456789, 987654321]);
    const usage = GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST;
    const texture = device.createTexture({ size: [1, 1], format: 'rg32uint', usage });
    const second = device.createTexture({ size: [1, 1], format: 'rg32uint', usage });
    // All three transfer APIs allow omitted strides for one row of one image.
    device.queue.writeTexture({ texture }, expected, {}, [1, 1]);
    const intermediate = device.createBuffer({ size: 8, usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST });
    const readback = device.createBuffer({ size: 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    const encoder = device.createCommandEncoder();
    encoder.copyTextureToBuffer({ texture, origin: [0, 0] }, { buffer: intermediate }, [1, 1]);
    encoder.copyBufferToTexture({ buffer: intermediate }, { texture: second }, [1, 1]);
    encoder.copyTextureToBuffer({ texture: second }, { buffer: readback }, [1, 1]);
    device.queue.submit([encoder.finish()]);
    await readback.mapAsync(GPUMapMode.READ);
    const actual = new Uint32Array(readback.getMappedRange());
    if (actual[0] !== expected[0] || actual[1] !== expected[1]) {
        throw new Error(`Readback mismatch: ${actual[0]}, ${actual[1]}`);
    }
    readback.unmap();
    readback.destroy();
    intermediate.destroy();
    texture.destroy();
    second.destroy();
    // Produce a canvas frame so the screenshot-based test runner can exit cleanly.
    const context = canvas.getContext('webgpu');
    context.configure({ device, format: navigator.gpu.getPreferredCanvasFormat() });
    const frame = device.createCommandEncoder();
    const pass = frame.beginRenderPass({ colorAttachments: [{
        view: context.getCurrentTexture().createView(),
        clearValue: { r: 0, g: 1, b: 0, a: 1 }, loadOp: 'clear', storeOp: 'store',
    }] });
    pass.end();
    device.queue.submit([frame.finish()]);
    console.log('TEXTURE_COPY_STRIDE_PASS');
})().catch(error => console.error('TEXTURE_COPY_STRIDE_FAIL', String(error)));
