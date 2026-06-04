#include <dialog/backend.h>
#include <log/log.h>

#include <emscripten.h>

#include <stdlib.h>
#include <string.h>

bool mel_dialog__plat_available(void) { return true; }

EM_JS(void, mel_dialog_js_open, (unsigned lo, unsigned hi, const char* accept_ptr, int accept_len, int multi, int save, int dir), {
    var accept = accept_ptr ? UTF8ToString(accept_ptr, accept_len) : "";
    var token_lo = lo, token_hi = hi;

    function persist(files, done) {
        if (!files || files.length === 0) { done([]); return; }
        try { FS.mkdir('/tmp'); } catch (e) {}
        try { FS.mkdir('/tmp/dialog'); } catch (e) {}
        var paths = [];
        var pending = files.length;
        for (var i = 0; i < files.length; i++) {
            (function(file) {
                var reader = new FileReader();
                reader.onload = function() {
                    var data = new Uint8Array(reader.result);
                    var p = '/tmp/dialog/' + file.name;
                    try { FS.writeFile(p, data); paths.push(p); } catch (e) {}
                    if (--pending === 0) done(paths);
                };
                reader.onerror = function() { if (--pending === 0) done(paths); };
                reader.readAsArrayBuffer(file);
            })(files[i]);
        }
    }

    function emit(paths) {
        var joined = paths.join('\n');
        var len = lengthBytesUTF8(joined);
        var ptr = _malloc(len + 1);
        stringToUTF8(joined, ptr, len + 1);
        _mel_dialog_web__on_result(token_lo, token_hi, ptr, paths.length);
    }

    if (save) {
        emit([]);
        return;
    }

    if (!dir && window.showOpenFilePicker) {
        var opts = { multiple: !!multi };
        window.showOpenFilePicker(opts)
            .then(function(handles) {
                var files = [];
                var pend = handles.length;
                if (pend === 0) { emit([]); return; }
                handles.forEach(function(h) {
                    h.getFile().then(function(f) { files.push(f); if (--pend === 0) persist(files, emit); });
                });
            })
            .catch(function() { emit([]); });
        return;
    }

    var input = document.createElement('input');
    input.type = 'file';
    if (multi) input.multiple = true;
    if (dir) input.webkitdirectory = true;
    if (accept) input.accept = accept;
    input.style.display = 'none';
    document.body.appendChild(input);
    input.addEventListener('change', function() {
        var files = Array.prototype.slice.call(input.files);
        document.body.removeChild(input);
        persist(files, emit);
    });
    window.addEventListener('focus', function onf() {
        window.removeEventListener('focus', onf);
        setTimeout(function() {
            if (input.parentNode && (!input.files || input.files.length === 0)) {
                document.body.removeChild(input);
                emit([]);
            }
        }, 500);
    });
    input.click();
})

EMSCRIPTEN_KEEPALIVE void mel_dialog_web__on_result(unsigned lo, unsigned hi, char* ptr, int count)
{
    Mel_Dialog_Job* job = mel_dialog__job_from_token(((u64)hi << 32) | (u64)lo);
    if (job)
    {
        if (ptr && count > 0)
        {
            char* s = ptr;
            char* start = s;
            for (;;)
            {
                char* nl = strchr(start, '\n');
                if (nl)
                    *nl = 0;
                if (*start)
                    mel_dialog_job_emit_path(job, start);
                if (!nl)
                    break;
                start = nl + 1;
            }
            mel_dialog_job_resolve(job, MEL_DIALOG_OK);
        }
        else
        {
            mel_dialog_job_resolve(job, MEL_DIALOG_OK | MEL_DIALOG_CANCELLED);
        }
    }
    if (ptr)
        free(ptr);
}

void mel_dialog__plat_run(Mel_Dialog_Job* job)
{
    u32 request = mel_dialog_job_request(job);
    if (mel_dialog_job_parent(job).index != 0)
        mel_dialog_job_add_warning(job, MEL_DIALOG_WARN_PARENT_IGNORED);

    char accept[512];
    accept[0] = 0;
    usize w = 0;
    u32   fc = mel_dialog_job_filter_count(job);
    for (u32 i = 0; i < fc && w + 8 < sizeof accept; i++)
    {
        u32 pc = mel_dialog_job_filter_pattern_count(job, i);
        for (u32 p = 0; p < pc && w + 8 < sizeof accept; p++)
        {
            const char* pat = mel_dialog_job_filter_pattern(job, i, p);
            if (!pat || strchr(pat, '*'))
                continue;
            const char* ext = strrchr(pat, '.');
            const char* dotpat = ext ? ext : pat;
            int         n = snprintf(accept + w, sizeof accept - w, "%s%s%s", w ? "," : "", (dotpat[0] == '.') ? "" : ".", dotpat);
            if (n > 0)
                w += (usize)n;
        }
    }
    if (request & MEL_DIALOG_REQUEST_SAVE_FILE)
        mel_dialog_job_add_warning(job, MEL_DIALOG_WARN_DEFAULT_PATH_IGNORED);

    mel_dialog_js_open((unsigned)(mel_dialog_job_token(job) & 0xffffffffu),
                       (unsigned)(mel_dialog_job_token(job) >> 32),
                       accept, (int)w,
                       (request & MEL_DIALOG_REQUEST_MULTI) ? 1 : 0,
                       (request & MEL_DIALOG_REQUEST_SAVE_FILE) ? 1 : 0,
                       (request & MEL_DIALOG_REQUEST_OPEN_DIR) ? 1 : 0);
}
