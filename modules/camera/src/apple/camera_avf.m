#include <camera/provider.h>

#include <image/image.h>
#include <image/format.h>
#include <image/geometry.h>

#include <log/log.h>

#import <TargetConditionals.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

@class Mel_AVF_Session;

@interface Mel_AVF_Session : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, strong) AVCaptureDevice* device;
@property(nonatomic, strong) AVCaptureSession* session;
@property(nonatomic, strong) AVCaptureVideoDataOutput* output;
@property(nonatomic, strong) dispatch_queue_t queue;
@property(nonatomic, assign) u64 stable_id;
@property(nonatomic, assign) Mel_Camera_Sink sink;
@property(nonatomic, assign) BOOL haveSink;
@property(nonatomic, assign) const mel_image_format* fmt;
@property(nonatomic, assign) BOOL flipX;
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

static NSMutableDictionary<NSNumber*, Mel_AVF_Session*>* avf_sessions(void)
{
    static NSMutableDictionary* d;
    static dispatch_once_t      once;
    dispatch_once(&once, ^{ d = [NSMutableDictionary dictionary]; });
    return d;
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

static u32 avf_enumerate(void* user, Mel_Camera_Raw* out, u32 cap)
{
    (void)user;
    @autoreleasepool
    {
        AVCaptureDeviceDiscoverySession* disco = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:avf_device_types() mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
        u32                              n = 0;
        for (AVCaptureDevice* dev in disco.devices)
        {
            if (n >= cap)
                break;
            const char* name = dev.localizedName.UTF8String;
            out[n].stable_id = avf_stable_id(dev);
            out[n].name = (str8){ (u8*)name, (size)(name ? strlen(name) : 0) };
            out[n].facing = avf_facing(dev);
            out[n].modes = NULL;
            out[n].mode_count = 0;
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

static bool avf_open(void* user, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink)
{
    (void)user;
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

        if (![dev lockForConfiguration:&err])
        {
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
        s.output.videoSettings = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(fourcc) };
        [s.output setSampleBufferDelegate:s queue:s.queue];

        [s.session beginConfiguration];
        if (![s.session canAddInput:input] || ![s.session canAddOutput:s.output])
        {
            [s.session commitConfiguration];
            mel_log_error("camera", "avf open: cannot add input/output");
            return false;
        }
        [s.session addInput:input];
        [s.session addOutput:s.output];
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
    };
    mel_camera_provider_register(&desc);
}
