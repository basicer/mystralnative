(async () => {
    const device = await (await navigator.gpu.requestAdapter()).requestDevice();
    const format = navigator.gpu.getPreferredCanvasFormat();
    const srgbFormat = `${format}-srgb`;
    const context = canvas.getContext('webgpu');

    async function check(ctx, srgb, allowSrgb = srgb) {
        ctx.configure({ device, format, ...(allowSrgb ? { viewFormats: [srgbFormat] } : {}) });
        const texture = ctx.getCurrentTexture();
        const view = srgb ? texture.createView({ format: srgbFormat }) : texture.createView();
        const buffer = device.createBuffer({ size: 256, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({ colorAttachments: [{
            view, clearValue: { r: 0.5, g: 0.5, b: 0.5, a: 1 }, loadOp: 'clear', storeOp: 'store',
        }] });
        pass.end();
        encoder.copyTextureToBuffer({ texture }, { buffer, bytesPerRow: 256 }, [1, 1, 1]);
        device.queue.submit([encoder.finish()]);
        await buffer.mapAsync(GPUMapMode.READ);
        const bytes = new Uint8Array(buffer.getMappedRange());
        const expected = srgb ? 188 : 128;
        for (let channel = 0; channel < 3; channel++) {
            if (Math.abs(bytes[channel] - expected) > 1) {
                throw new Error(`Expected ${expected}, got ${bytes[channel]} (sRGB=${srgb})`);
            }
        }
        if (bytes[3] !== 255) throw new Error('Incorrect alpha');
        buffer.unmap();
        buffer.destroy();
    }

    await check(context, true);
    await check(context, false, true); // Declaring sRGB must not change the default view.
    await check(context, false); // Reconfigure without alternate formats; default stays linear.
    await check(context, true);
    const elementContext = document.createElement('canvas').getContext('webgpu');
    await check(elementContext, true);
    await check(elementContext, false);
    console.log('CANVAS_SRGB_PASS');
})().catch(error => console.error('CANVAS_SRGB_FAIL', String(error)));
