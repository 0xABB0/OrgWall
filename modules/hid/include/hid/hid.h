#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Port     Mel_Port;
typedef struct Mel_Future   Mel_Future;

typedef u32 Mel_Hid_Status;

#define MEL_HID_SEVERITY_MASK  0x3u
#define MEL_HID_OK             0u
#define MEL_HID_WARNED         1u
#define MEL_HID_ERROR          2u

#define MEL_HID_TIMED_OUT      (1u << 2)
#define MEL_HID_WOULD_BLOCK    (1u << 3)
#define MEL_HID_DEVICE_LOST    (1u << 4)
#define MEL_HID_PARTIAL        (1u << 5)
#define MEL_HID_ACCESS_DENIED  (1u << 6)
#define MEL_HID_UNSUPPORTED    (1u << 7)
#define MEL_HID_NOT_OPEN       (1u << 8)
#define MEL_HID_INVALID_HANDLE (1u << 9)
#define MEL_HID_CANCELLED      (1u << 10)
#define MEL_HID_NO_BACKEND     (1u << 11)

static inline bool mel_hid_failed(Mel_Hid_Status s) { return (s & MEL_HID_SEVERITY_MASK) == MEL_HID_ERROR; }
static inline bool mel_hid_warned(Mel_Hid_Status s) { return (s & MEL_HID_SEVERITY_MASK) == MEL_HID_WARNED; }
static inline bool mel_hid_timed_out(Mel_Hid_Status s) { return (s & MEL_HID_TIMED_OUT) != 0u; }
static inline bool mel_hid_would_block(Mel_Hid_Status s) { return (s & MEL_HID_WOULD_BLOCK) != 0u; }
static inline bool mel_hid_device_lost(Mel_Hid_Status s) { return (s & MEL_HID_DEVICE_LOST) != 0u; }

typedef u32 Mel_Hid_Bus;

#define MEL_HID_BUS_UNKNOWN   0u
#define MEL_HID_BUS_USB       1u
#define MEL_HID_BUS_BLUETOOTH 2u
#define MEL_HID_BUS_I2C       3u
#define MEL_HID_BUS_SPI       4u

#define MEL_HID_STRING_CAP    256

typedef struct
{
    u16 vendor_id;
    u16 product_id;
    u16 version_bcd;

    u16 usage_page;
    u16 usage;

    Mel_Hid_Bus bus;

    u16 input_report_len;
    u16 output_report_len;
    u16 feature_report_len;

    bool has_report_id;
    u8   report_id_count;

    char manufacturer[MEL_HID_STRING_CAP];
    char product[MEL_HID_STRING_CAP];
    char serial[MEL_HID_STRING_CAP];

    char path[MEL_HID_STRING_CAP];
} Mel_Hid_Descriptor;

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Hid_Device;

#define MEL_HID_DEVICE_NULL ((Mel_Hid_Device){ 0 })

typedef struct
{
    Mel_Hid_Descriptor value;
    Mel_Hid_Status     status;
} Mel_Hid_Describe_Result;

typedef struct
{
    usize          bytes;
    Mel_Hid_Status status;
} Mel_Hid_Io_Result;

#define MEL_HID_TIMEOUT_BLOCK ((i32) - 1)
#define MEL_HID_TIMEOUT_POLL  ((i32)0)

void mel_hid_init(const Mel_Alloc* alloc);
void mel_hid_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec);
void mel_hid_shutdown(void);

u32 mel_hid_refresh(void);
u32 mel_hid_count(void);
u32 mel_hid_list(Mel_Hid_Device* out, u32 cap);

u64 mel_hid_device_change_count(void);

Mel_Hid_Describe_Result mel_hid_describe(Mel_Hid_Device d);
bool                    mel_hid_alive(Mel_Hid_Device d);
bool                    mel_hid_equal(Mel_Hid_Device a, Mel_Hid_Device b);

Mel_Hid_Status mel_hid_open(Mel_Hid_Device d);
void           mel_hid_close(Mel_Hid_Device d);
bool           mel_hid_is_open(Mel_Hid_Device d);

Mel_Hid_Io_Result mel_hid_write(Mel_Hid_Device d, const u8* data, usize len);
Mel_Hid_Io_Result mel_hid_read(Mel_Hid_Device d, u8* out, usize cap, i32 timeout_ms);

Mel_Hid_Io_Result mel_hid_get_feature(Mel_Hid_Device d, u8 report_id, u8* out, usize cap);
Mel_Hid_Io_Result mel_hid_send_feature(Mel_Hid_Device d, const u8* data, usize len);

Mel_Hid_Io_Result mel_hid_get_report_descriptor(Mel_Hid_Device d, u8* out, usize cap);
Mel_Hid_Io_Result mel_hid_get_string(Mel_Hid_Device d, u8 string_index, u8* out, usize cap);

typedef struct
{
    u8*           buffer;
    usize         len;
    Mel_Port*     port;
    Mel_Executor* deliver;
} Mel_Hid_Read_Async_Opt;

Mel_Future* mel_hid_read_async_opt(Mel_Hid_Device d, Mel_Hid_Read_Async_Opt opt);
#define mel_hid_read_async(d, ...) mel_hid_read_async_opt((d), (Mel_Hid_Read_Async_Opt){ __VA_ARGS__ })

const Mel_Hid_Io_Result* mel_hid_future_result(Mel_Future* f);
void                     mel_hid_future_release(Mel_Future* f);

void* mel_hid_native(Mel_Hid_Device d);

#ifdef __cplusplus
}
#endif
