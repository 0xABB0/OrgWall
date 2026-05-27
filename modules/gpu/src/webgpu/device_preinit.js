// emscripten_webgpu_get_device() (used by the webgpu backend's device create)
// returns Module.preinitializedWebGPUDevice, which must exist before main runs.
// Acquiring a WebGPU device is async, so gate startup on a run dependency until
// the adapter + device resolve.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
    if (!navigator.gpu) {
        console.error('mel_gpu: WebGPU is not available in this browser');
        return;
    }
    addRunDependency('mel-webgpu-device');
    navigator.gpu.requestAdapter()
        .then(function (adapter) { return adapter.requestDevice(); })
        .then(function (device) {
            Module['preinitializedWebGPUDevice'] = device;
            removeRunDependency('mel-webgpu-device');
        })
        .catch(function (err) {
            console.error('mel_gpu: failed to acquire a WebGPU device', err);
            removeRunDependency('mel-webgpu-device');
        });
});
