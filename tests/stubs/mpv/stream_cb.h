#pragma once
#include <cstdint>
#include "client.h"
using mpv_stream_cb_read_fn = std::int64_t(*)(void *, char *, std::uint64_t);
using mpv_stream_cb_seek_fn = std::int64_t(*)(void *, std::int64_t);
using mpv_stream_cb_size_fn = std::int64_t(*)(void *);
using mpv_stream_cb_close_fn = void(*)(void *);
using mpv_stream_cb_cancel_fn = void(*)(void *);
struct mpv_stream_cb_info {
 void *cookie{};
 mpv_stream_cb_read_fn read_fn{};
 mpv_stream_cb_seek_fn seek_fn{};
 mpv_stream_cb_size_fn size_fn{};
 mpv_stream_cb_close_fn close_fn{};
 mpv_stream_cb_cancel_fn cancel_fn{};
};
using mpv_stream_cb_open_ro_fn = int(*)(void *,char *,mpv_stream_cb_info *);
inline mpv_stream_cb_open_ro_fn g_open_fn=nullptr;
inline void* g_user_data=nullptr;
inline int mpv_stream_cb_add_ro(mpv_handle*, const char* scheme,void* context, mpv_stream_cb_open_ro_fn cb) {
 if(!scheme || !cb)return MPV_ERROR_INVALID_PARAMETER;
 g_open_fn=cb;g_user_data=context;return 0;
}
