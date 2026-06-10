#include <camera/provider.h>

#include <image/image.h>
#include <image/format.h>
#include <image/geometry.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>

#include <debug/assert.h>
#include <log/log.h>

#include <string.h>

#import <TargetConditionals.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

@class Mel_AVF_Session;

@interface                                             Mel_AVF_Session: NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, strong) AVCaptureDevice*          device;
@property(nonatomic, strong) AVCaptureSession*         session;
@property(nonatomic, strong) AVCaptureVideoDataOutput* output;
@property(nonatomic, strong) dispatch_queue_t          queue;
@property(nonatomic, assign) u64                       stable_id;
@property(nonatomic, assign) Mel_Camera_Sink           sink;
@property(nonatomic, assign) BOOL                      haveSink;
@property(nonatomic, assign) const mel_image_format*   fmt;
@property(nonatomic, assign) BOOL                      flipX;
@end

@implementation Mel_AVF_Session

- (void)captureOutput:(AVCaptureOutput*)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection*)connection
{
    (void)output;
    (void)connection;
    if (!self.haveSink || self.sink.on_frame == NULL)
        return;

    CVImageBufferRef pixel = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!pixel)
        return;

    const mel_image_format* fmt = self.fmt;
    if (!fmt)
        return;

    CVPixelBufferLockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);

    i32 w = (i32)CVPixelBufferGetWidth(pixel);
    i32 h = (i32)CVPixelBufferGetHeight(pixel);

    Mel_Image image;
    bool      wrapped = false;

    if (CVPixelBufferIsPlanar(pixel))
    {
        usize plane_count = CVPixelBufferGetPlaneCount(pixel);
        if (plane_count == 2)
        {
            Mel_Image_Plane planes[2];
            for (usize p = 0; p < 2; p++)
            {
                planes[p].pixels = (u8*)CVPixelBufferGetBaseAddressOfPlane(pixel, p);
                planes[p].stride = (i32)CVPixelBufferGetBytesPerRowOfPlane(pixel, p);
                planes[p].w = (i32)CVPixelBufferGetWidthOfPlane(pixel, p);
                planes[p].h = (i32)CVPixelBufferGetHeightOfPlane(pixel, p);
                planes[p].bpp = p == 0 ? 1 : 2;
            }
            wrapped = mel_image_wrap(&image, fmt, w, h, planes, 2);
        }
        else
            mel_log_error("camera", "avf frame: unsupported planar count %zu", plane_count);
    }
    else
    {
        Mel_Image_Plane plane;
        plane.pixels = (u8*)CVPixelBufferGetBaseAddress(pixel);
        plane.stride = (i32)CVPixelBufferGetBytesPerRow(pixel);
        plane.w = w;
        plane.h = h;
        plane.bpp = 4;
        wrapped = mel_image_wrap(&image, fmt, w, h, &plane, 1);
    }

    if (wrapped)
    {
        CMTime           pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        u64              ns = (CMTIME_IS_VALID(pts) && pts.timescale != 0) ? (u64)pts.value * 1000000000ull / (u64)pts.timescale : 0;
        Mel_Camera_Frame frame = {
            .image = image,
            .timestamp_ns = ns,
            .sequence = 0,
            .orient = { .quarter_turns = 0, .flip_x = self.flipX ? true : false },
        };
        self.sink.on_frame(self.sink.token, &frame);
    }

    CVPixelBufferUnlockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
}

@end

static NSMutableDictionary<NSNumber*, Mel_AVF_Session*>* g_avf_sessions;

static NSMutableDictionary<NSNumber*, Mel_AVF_Session*>* avf_sessions(void)
{
    if (g_avf_sessions == nil)
        g_avf_sessions = [NSMutableDictionary dictionary];
    return g_avf_sessions;
}

static u64 avf_stable_id(AVCaptureDevice* dev) { return (u64)[dev.uniqueID hash]; }

#if TARGET_OS_OSX
static AVCaptureDeviceType avf_external_type(void)
{
    if (@available(macOS 14.0, *))
        return AVCaptureDeviceTypeExternal;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return AVCaptureDeviceTypeExternalUnknown;
#pragma clang diagnostic pop
}
#endif

static const mel_camera_facing* avf_facing(AVCaptureDevice* dev)
{
    switch (dev.position)
    {
    case AVCaptureDevicePositionFront:
        return &mel_camera_front;
    case AVCaptureDevicePositionBack:
        return &mel_camera_back;
    default:
#if TARGET_OS_OSX
        return [dev.deviceType isEqualToString:avf_external_type()] ? &mel_camera_external : &mel_camera_unknown;
#else
        return &mel_camera_unknown;
#endif
    }
}

#if TARGET_OS_OSX
static NSArray<AVCaptureDeviceType>* avf_device_types(void) { return @[ AVCaptureDeviceTypeBuiltInWideAngleCamera, avf_external_type() ]; }
#else
static NSArray<AVCaptureDeviceType>* avf_device_types(void) { return @[ AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeBuiltInUltraWideCamera, AVCaptureDeviceTypeBuiltInTelephotoCamera ]; }
#endif

typedef struct
{
    const mel_image_format* fmt;
    OSType                  fourcc;
} Avf_Format_Map;

static const Avf_Format_Map* avf_format_map(usize* count)
{
    static const Avf_Format_Map map[] = {
        { &mel_image_nv12, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange },
        { &mel_image_nv12_full, kCVPixelFormatType_420YpCbCr8BiPlanarFullRange },
        { &mel_image_bgra8, kCVPixelFormatType_32BGRA },
    };
    *count = sizeof(map) / sizeof(map[0]);
    return map;
}

static bool avf_fourcc_for(const mel_image_format* fmt, OSType* out)
{
    usize                 n = 0;
    const Avf_Format_Map* map = avf_format_map(&n);
    for (usize i = 0; i < n; i++)
        if (map[i].fmt == fmt)
        {
            *out = map[i].fourcc;
            return true;
        }
    return false;
}

static const mel_image_format* avf_format_for(OSType fourcc)
{
    usize                 n = 0;
    const Avf_Format_Map* map = avf_format_map(&n);
    for (usize i = 0; i < n; i++)
        if (map[i].fourcc == fourcc)
            return map[i].fmt;
    return NULL;
}

static AVCaptureDeviceFormat* avf_select_format(AVCaptureDevice* dev, OSType fourcc, i32 w, i32 h)
{
    for (AVCaptureDeviceFormat* f in dev.formats)
    {
        CMFormatDescriptionRef desc = f.formatDescription;
        if (CMFormatDescriptionGetMediaSubType(desc) != fourcc)
            continue;
        CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(desc);
        if (dim.width == w && dim.height == h)
            return f;
    }
    return nil;
}

static AVCaptureDevice* avf_device_for(u64 stable_id)
{
    AVCaptureDeviceDiscoverySession* disco = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:avf_device_types() mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
    for (AVCaptureDevice* dev in disco.devices)
        if (avf_stable_id(dev) == stable_id)
            return dev;
    return nil;
}

typedef struct
{
    u64  stable_id;
    str8 name;
} Avf_Name_Rec;

typedef struct
{
    u64 stable_id;
    Mel_Array(Mel_Camera_Mode) modes;
} Avf_Modes_Rec;

static struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Avf_Name_Rec) names;
    Mel_Array(Avf_Modes_Rec) modes;
} g_avf;

static void avf_names_clear(void)
{
    for (usize i = 0; i < g_avf.names.count; i++)
        if (g_avf.names.items[i].name.data)
            mel_dealloc(g_avf.alloc, g_avf.names.items[i].name.data);
    mel_array_clear(&g_avf.names);
}

static void avf_modes_clear(void)
{
    for (usize i = 0; i < g_avf.modes.count; i++)
        mel_array_free(&g_avf.modes.items[i].modes);
    mel_array_clear(&g_avf.modes);
}

static u32 avf_collect_modes(AVCaptureDevice* dev, u64 stable_id, const Mel_Camera_Mode** out_modes)
{
    Avf_Modes_Rec rec = { .stable_id = stable_id };
    mel_array_init(&rec.modes, g_avf.alloc);

    for (AVCaptureDeviceFormat* f in dev.formats)
    {
        CMFormatDescriptionRef  desc = f.formatDescription;
        const mel_image_format* fmt = avf_format_for(CMFormatDescriptionGetMediaSubType(desc));
        if (fmt == NULL)
            continue;

        CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(desc);
        f32               fps_min = 0.0f;
        f32               fps_max = 0.0f;
        bool              have = false;
        for (AVFrameRateRange* r in f.videoSupportedFrameRateRanges)
        {
            f32 lo = (f32)r.minFrameRate;
            f32 hi = (f32)r.maxFrameRate;
            if (!have)
            {
                fps_min = lo;
                fps_max = hi;
                have = true;
            }
            else
            {
                if (lo < fps_min)
                    fps_min = lo;
                if (hi > fps_max)
                    fps_max = hi;
            }
        }

        bool merged = false;
        for (usize i = 0; i < rec.modes.count; i++)
        {
            Mel_Camera_Mode* m = &rec.modes.items[i];
            if (m->format == fmt && m->width == dim.width && m->height == dim.height)
            {
                if (fps_min < m->fps_min)
                    m->fps_min = fps_min;
                if (fps_max > m->fps_max)
                    m->fps_max = fps_max;
                merged = true;
                break;
            }
        }
        if (merged)
            continue;

        Mel_Camera_Mode m = { .format = fmt, .width = dim.width, .height = dim.height, .fps_min = fps_min, .fps_max = fps_max };
        mel_array_push(&rec.modes, m);
    }

    mel_array_push(&g_avf.modes, rec);
    *out_modes = rec.modes.items;
    return (u32)rec.modes.count;
}

static str8 avf_intern_name(u64 stable_id, const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_avf.alloc, len + 1);
    if (!data)
        return (str8){ 0 };
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    Avf_Name_Rec rec = { .stable_id = stable_id, .name = (str8){ data, (size)len } };
    mel_array_push(&g_avf.names, rec);
    return rec.name;
}

static u32 avf_enumerate(void* user, const Mel_Alloc* alloc, Mel_Camera_Raw* out, u32 cap)
{
    (void)user;
    g_avf.alloc = alloc;
    if (g_avf.names.allocator == NULL)
        mel_array_init(&g_avf.names, alloc);
    mel_assert(g_avf.names.allocator == alloc && "avf_enumerate: allocator changed across calls");
    avf_names_clear();
    if (g_avf.modes.allocator == NULL)
        mel_array_init(&g_avf.modes, alloc);
    avf_modes_clear();
    @autoreleasepool
    {
        AVCaptureDeviceDiscoverySession* disco = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:avf_device_types() mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
        u32                              n = 0;
        for (AVCaptureDevice* dev in disco.devices)
        {
            if (n >= cap)
                break;
            u64 stable_id = avf_stable_id(dev);
            out[n].stable_id = stable_id;
            out[n].name = avf_intern_name(stable_id, dev.localizedName.UTF8String);
            out[n].facing = avf_facing(dev);
            out[n].mode_count = avf_collect_modes(dev, stable_id, &out[n].modes);
            n++;
        }
        return n;
    }
}

static const mel_camera_auth* avf_authorization(void* user)
{
    (void)user;
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo])
    {
    case AVAuthorizationStatusAuthorized:
        return &mel_camera_auth_granted;
    case AVAuthorizationStatusDenied:
        return &mel_camera_auth_denied;
    case AVAuthorizationStatusRestricted:
        return &mel_camera_auth_restricted;
    default:
        return &mel_camera_auth_not_determined;
    }
}

static void avf_authorize(void* user, Mel_Camera_Sink sink)
{
    (void)user;
    if (sink.on_auth == NULL)
        return;
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                             completionHandler:^(BOOL granted) {
                                 sink.on_auth(sink.token, granted ? &mel_camera_auth_granted : &mel_camera_auth_denied);
                             }];
}

static bool avf_open(void* user, const Mel_Alloc* alloc, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink)
{
    (void)user;
    (void)alloc;
    @autoreleasepool
    {
        if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] != AVAuthorizationStatusAuthorized)
        {
            mel_log_error("camera", "avf open: camera not authorized; call mel_camera_authorize first");
            return false;
        }

        AVCaptureDevice* dev = avf_device_for(stable_id);
        if (!dev)
        {
            mel_log_error("camera", "avf open: device %llu not found", (unsigned long long)stable_id);
            return false;
        }

        OSType fourcc = 0;
        if (!avf_fourcc_for(cfg.format, &fourcc))
        {
            mel_log_error("camera", "avf open: pixel format unsupported by backend");
            return false;
        }

        AVCaptureDeviceFormat* devFormat = avf_select_format(dev, fourcc, cfg.width, cfg.height);
        if (!devFormat)
        {
            mel_log_error("camera", "avf open: no device format for %dx%d fourcc 0x%08x", cfg.width, cfg.height, (unsigned)fourcc);
            return false;
        }

        NSError*              err = nil;
        AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:dev error:&err];
        if (!input)
        {
            mel_log_error("camera", "avf open: input failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }

        Mel_AVF_Session* s = [[Mel_AVF_Session alloc] init];
        s.stable_id = stable_id;
        s.device = dev;
        s.sink = sink;
        s.haveSink = YES;
        s.fmt = cfg.format;
        s.flipX = (dev.position == AVCaptureDevicePositionFront);
        s.session = [[AVCaptureSession alloc] init];
        s.queue = dispatch_queue_create("melody.camera.avf", DISPATCH_QUEUE_SERIAL);
        s.output = [[AVCaptureVideoDataOutput alloc] init];
        s.output.alwaysDiscardsLateVideoFrames = YES;
#if TARGET_OS_OSX
        s.output.videoSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(fourcc),
            (id)kCVPixelBufferWidthKey : @(cfg.width),
            (id)kCVPixelBufferHeightKey : @(cfg.height),
        };
#else
        s.output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(fourcc)};
#endif
        [s.output setSampleBufferDelegate:s queue:s.queue];

        [s.session beginConfiguration];
#if !TARGET_OS_OSX
        s.session.sessionPreset = AVCaptureSessionPresetInputPriority;
#endif
        if (![s.session canAddInput:input] || ![s.session canAddOutput:s.output])
        {
            [s.session commitConfiguration];
            mel_log_error("camera", "avf open: cannot add input/output");
            return false;
        }
        [s.session addInput:input];
        [s.session addOutput:s.output];

        if (![dev lockForConfiguration:&err])
        {
            [s.session commitConfiguration];
            mel_log_error("camera", "avf open: lockForConfiguration failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }
        dev.activeFormat = devFormat;
        if (cfg.fps > 0.0f)
        {
            CMTime dur = CMTimeMake(1, (int32_t)(cfg.fps + 0.5f));
            dev.activeVideoMinFrameDuration = dur;
            dev.activeVideoMaxFrameDuration = dur;
        }
        [dev unlockForConfiguration];
        [s.session commitConfiguration];

        avf_sessions()[@(stable_id)] = s;
        return true;
    }
}

static void avf_close(void* user, u64 stable_id)
{
    (void)user;
    @autoreleasepool
    {
        Mel_AVF_Session* s = avf_sessions()[@(stable_id)];
        if (!s)
            return;
        if (s.session.running)
            [s.session stopRunning];
        s.haveSink = NO;
        [avf_sessions() removeObjectForKey:@(stable_id)];
    }
}

static Mel_Camera_Status avf_start(void* user, u64 stable_id)
{
    (void)user;
    @autoreleasepool
    {
        Mel_AVF_Session* s = avf_sessions()[@(stable_id)];
        if (!s)
            return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
        if (!s.session.running)
            [s.session startRunning];
        return MEL_CAMERA_OK;
    }
}

static Mel_Camera_Status avf_stop(void* user, u64 stable_id)
{
    (void)user;
    @autoreleasepool
    {
        Mel_AVF_Session* s = avf_sessions()[@(stable_id)];
        if (!s)
            return MEL_CAMERA_ERROR | MEL_CAMERA_RESULT_NO_DEVICE;
        if (s.session.running)
            [s.session stopRunning];
        return MEL_CAMERA_OK;
    }
}

static void* avf_native(void* user, u64 stable_id)
{
    (void)user;
    Mel_AVF_Session* s = avf_sessions()[@(stable_id)];
    return s ? (__bridge void*)s.session : NULL;
}

static void avf_shutdown(void* user, const Mel_Alloc* alloc)
{
    (void)user;
    (void)alloc;
    @autoreleasepool
    {
        if (g_avf_sessions != nil)
        {
            for (NSNumber* key in [g_avf_sessions allKeys])
            {
                Mel_AVF_Session* s = g_avf_sessions[key];
                if (s.session.running)
                    [s.session stopRunning];
                s.haveSink = NO;
            }
            [g_avf_sessions removeAllObjects];
            g_avf_sessions = nil;
        }
    }
    if (g_avf.names.allocator != NULL)
    {
        avf_names_clear();
        mel_array_free(&g_avf.names);
        mel_array_init(&g_avf.names, NULL);
    }
    if (g_avf.modes.allocator != NULL)
    {
        avf_modes_clear();
        mel_array_free(&g_avf.modes);
        mel_array_init(&g_avf.modes, NULL);
    }
    g_avf.alloc = NULL;
}

void mel_camera__register_host_providers(void)
{
    static const Mel_Camera_Provider_Desc desc = {
        .name = "apple-avfoundation",
        .enumerate = avf_enumerate,
        .open = avf_open,
        .close = avf_close,
        .start = avf_start,
        .stop = avf_stop,
        .authorization = avf_authorization,
        .authorize = avf_authorize,
        .native = avf_native,
        .shutdown = avf_shutdown,
    };
    mel_camera_provider_register(&desc);
}
