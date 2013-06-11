// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/renderer/media/android/media_source_delegate.h"

#include "base/message_loop/message_loop_proxy.h"
#include "base/strings/string_number_conversions.h"
#include "media/base/android/demuxer_stream_player_params.h"
#include "media/base/bind_to_loop.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_log.h"
#include "media/filters/chunk_demuxer.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebMediaSource.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebRuntimeFeatures.h"
#include "webkit/renderer/media/android/webmediaplayer_proxy_android.h"
#include "webkit/renderer/media/crypto/key_systems.h"
#include "webkit/renderer/media/webmediaplayer_util.h"
#include "webkit/renderer/media/webmediasourceclient_impl.h"

using media::DemuxerStream;
using media::MediaPlayerHostMsg_DemuxerReady_Params;
using media::MediaPlayerHostMsg_ReadFromDemuxerAck_Params;
using WebKit::WebMediaPlayer;
using WebKit::WebString;

namespace {

// The size of the access unit to transfer in an IPC in case of MediaSource.
// 16: approximately 250ms of content in 60 fps movies.
const size_t kAccessUnitSizeForMediaSource = 16;

const uint8 kVorbisPadding[] = { 0xff, 0xff, 0xff, 0xff };

}  // namespace

namespace webkit_media {

#define BIND_TO_RENDER_LOOP(function) \
  media::BindToLoop(base::MessageLoopProxy::current(), \
                    base::Bind(function, weak_this_.GetWeakPtr()))

#define BIND_TO_RENDER_LOOP_1(function, arg1) \
  media::BindToLoop(base::MessageLoopProxy::current(), \
                    base::Bind(function, weak_this_.GetWeakPtr(), arg1))

#define BIND_TO_RENDER_LOOP_2(function, arg1, arg2) \
  media::BindToLoop(base::MessageLoopProxy::current(), \
                    base::Bind(function, weak_this_.GetWeakPtr(), arg1, arg2))

#define BIND_TO_RENDER_LOOP_3(function, arg1, arg2, arg3) \
  media::BindToLoop(base::MessageLoopProxy::current(), \
                    base::Bind(function, \
                               weak_this_.GetWeakPtr(), arg1, arg2, arg3))

static void LogMediaSourceError(const scoped_refptr<media::MediaLog>& media_log,
                                const std::string& error) {
  media_log->AddEvent(media_log->CreateMediaSourceErrorEvent(error));
}

MediaSourceDelegate::MediaSourceDelegate(WebMediaPlayerProxyAndroid* proxy,
                                         int player_id,
                                         media::MediaLog* media_log)
    : weak_this_(this),
      proxy_(proxy),
      player_id_(player_id),
      media_log_(media_log),
      demuxer_(NULL),
      audio_params_(new MediaPlayerHostMsg_ReadFromDemuxerAck_Params),
      video_params_(new MediaPlayerHostMsg_ReadFromDemuxerAck_Params),
      seeking_(false),
      access_unit_size_(0) {
}

MediaSourceDelegate::~MediaSourceDelegate() {
  DVLOG(1) << "MediaSourceDelegate::~MediaSourceDelegate() : " << player_id_;
  DCHECK(!chunk_demuxer_);
  DCHECK(!demuxer_);
}

void MediaSourceDelegate::Destroy() {
  DVLOG(1) << "MediaSourceDelegate::Destroy() : " << player_id_;
  if (!demuxer_) {
    delete this;
    return;
  }

  update_network_state_cb_.Reset();
  media_source_.reset();
  proxy_ = NULL;

  demuxer_ = NULL;
  if (chunk_demuxer_)
    chunk_demuxer_->Stop(
        BIND_TO_RENDER_LOOP(&MediaSourceDelegate::OnDemuxerStopDone));
}

void MediaSourceDelegate::InitializeMediaSource(
    WebKit::WebMediaSource* media_source,
    const media::NeedKeyCB& need_key_cb,
    const UpdateNetworkStateCB& update_network_state_cb) {
  DCHECK(media_source);
  media_source_.reset(media_source);
  need_key_cb_ = need_key_cb;
  update_network_state_cb_ = update_network_state_cb;

  chunk_demuxer_.reset(new media::ChunkDemuxer(
      BIND_TO_RENDER_LOOP(&MediaSourceDelegate::OnDemuxerOpened),
      BIND_TO_RENDER_LOOP_2(&MediaSourceDelegate::OnNeedKey, "", ""),
      base::Bind(&MediaSourceDelegate::OnAddTextTrack,
                 base::Unretained(this)),
      base::Bind(&LogMediaSourceError, media_log_)));
  chunk_demuxer_->Initialize(this,
      BIND_TO_RENDER_LOOP(&MediaSourceDelegate::OnDemuxerInitDone));
  demuxer_ = chunk_demuxer_.get();
  access_unit_size_ = kAccessUnitSizeForMediaSource;
}

#if defined(GOOGLE_TV)
void MediaSourceDelegate::InitializeMediaStream(
    media::Demuxer* demuxer,
    const UpdateNetworkStateCB& update_network_state_cb) {
  DCHECK(demuxer);
  demuxer_ = demuxer;
  update_network_state_cb_ = update_network_state_cb;

  demuxer_->Initialize(this,
      BIND_TO_RENDER_LOOP(&MediaSourceDelegate::OnDemuxerInitDone));
  // When playing Media Stream, don't wait to accumulate multiple packets per
  // IPC communication.
  access_unit_size_ = 1;
}
#endif

const WebKit::WebTimeRanges& MediaSourceDelegate::Buffered() {
  buffered_web_time_ranges_ =
      ConvertToWebTimeRanges(buffered_time_ranges_);
  return buffered_web_time_ranges_;
}

size_t MediaSourceDelegate::DecodedFrameCount() const {
  return statistics_.video_frames_decoded;
}

size_t MediaSourceDelegate::DroppedFrameCount() const {
  return statistics_.video_frames_dropped;
}

size_t MediaSourceDelegate::AudioDecodedByteCount() const {
  return statistics_.audio_bytes_decoded;
}

size_t MediaSourceDelegate::VideoDecodedByteCount() const {
  return statistics_.video_bytes_decoded;
}

void MediaSourceDelegate::Seek(base::TimeDelta time) {
  DVLOG(1) << "MediaSourceDelegate::Seek(" << time.InSecondsF() << ") : "
           << player_id_;
  seeking_ = true;
  DCHECK(demuxer_);
  if (chunk_demuxer_)
    chunk_demuxer_->StartWaitingForSeek();
  demuxer_->Seek(time,
      BIND_TO_RENDER_LOOP(&MediaSourceDelegate::OnDemuxerError));
}

void MediaSourceDelegate::CancelPendingSeek() {
  if (chunk_demuxer_)
    chunk_demuxer_->CancelPendingSeek();
}

void MediaSourceDelegate::SetTotalBytes(int64 total_bytes) {
  NOTIMPLEMENTED();
}

void MediaSourceDelegate::AddBufferedByteRange(int64 start, int64 end) {
  NOTIMPLEMENTED();
}

void MediaSourceDelegate::AddBufferedTimeRange(base::TimeDelta start,
                                               base::TimeDelta end) {
  buffered_time_ranges_.Add(start, end);
}

void MediaSourceDelegate::SetDuration(base::TimeDelta duration) {
  // Do nothing
}

void MediaSourceDelegate::OnReadFromDemuxer(media::DemuxerStream::Type type,
                                            bool seek_done) {
  DVLOG(1) << "MediaSourceDelegate::OnReadFromDemuxer(" << type
           << ", " << seek_done << ") : " << player_id_;
  if (seeking_ && !seek_done)
      return;  // Drop the request during seeking.
  seeking_ = false;

  DCHECK(type == DemuxerStream::AUDIO || type == DemuxerStream::VIDEO);
  // The access unit size should have been initialized properly at this stage.
  DCHECK_GT(access_unit_size_, 0u);
  MediaPlayerHostMsg_ReadFromDemuxerAck_Params* params =
      type == DemuxerStream::AUDIO ? audio_params_.get() : video_params_.get();
  params->type = type;
  params->access_units.resize(access_unit_size_);
  DemuxerStream* stream = demuxer_->GetStream(type);
  DCHECK(stream != NULL);
  ReadFromDemuxerStream(stream, params, 0);
}

void MediaSourceDelegate::ReadFromDemuxerStream(
    DemuxerStream* stream,
    MediaPlayerHostMsg_ReadFromDemuxerAck_Params* params,
    size_t index) {
  stream->Read(BIND_TO_RENDER_LOOP_3(&MediaSourceDelegate::OnBufferReady,
                                     stream, params, index));
}

void MediaSourceDelegate::OnBufferReady(
    DemuxerStream* stream,
    MediaPlayerHostMsg_ReadFromDemuxerAck_Params* params,
    size_t index,
    DemuxerStream::Status status,
    const scoped_refptr<media::DecoderBuffer>& buffer) {
  DVLOG(1) << "MediaSourceDelegate::OnBufferReady() : " << player_id_;
  DCHECK(status == DemuxerStream::kAborted ||
         index < params->access_units.size());
  bool is_audio = stream->type() == DemuxerStream::AUDIO;
  if (status != DemuxerStream::kAborted &&
      index >= params->access_units.size()) {
    LOG(ERROR) << "The internal state inconsistency onBufferReady: "
               << (is_audio ? "Audio" : "Video") << ", index " << index
               <<", size " << params->access_units.size()
               << ", status " << static_cast<int>(status);
    return;
  }
  switch (status) {
    case DemuxerStream::kAborted:
      // Because the abort was caused by the seek, don't respond ack.
      return;

    case DemuxerStream::kConfigChanged:
      // In case of kConfigChanged, need to read decoder_config once
      // for the next reads.
      if (is_audio) {
        stream->audio_decoder_config();
      } else {
        gfx::Size size = stream->video_decoder_config().coded_size();
        DVLOG(1) << "Video config is changed: " <<
            size.width() << "x" << size.height();
      }
      params->access_units[index].status = status;
      params->access_units.resize(index + 1);
      break;

    case DemuxerStream::kOk:
      params->access_units[index].status = status;
      if (buffer->IsEndOfStream()) {
        params->access_units[index].end_of_stream = true;
        params->access_units.resize(index + 1);
        break;
      }
      // TODO(ycheo): We assume that the inputed stream will be decoded
      // right away.
      // Need to implement this properly using MediaPlayer.OnInfoListener.
      if (is_audio) {
        statistics_.audio_bytes_decoded += buffer->GetDataSize();
      } else {
        statistics_.video_bytes_decoded += buffer->GetDataSize();
        statistics_.video_frames_decoded++;
      }
      params->access_units[index].timestamp = buffer->GetTimestamp();
      params->access_units[index].data = std::vector<uint8>(
          buffer->GetData(),
          buffer->GetData() + buffer->GetDataSize());
#if !defined(GOOGLE_TV)
      // Vorbis needs 4 extra bytes padding on Android. Check
      // NuMediaExtractor.cpp in Android source code.
      if (is_audio && media::kCodecVorbis ==
          stream->audio_decoder_config().codec()) {
        params->access_units[index].data.insert(
            params->access_units[index].data.end(), kVorbisPadding,
            kVorbisPadding + 4);
      }
#endif
      if (buffer->GetDecryptConfig()) {
        params->access_units[index].key_id = std::vector<char>(
            buffer->GetDecryptConfig()->key_id().begin(),
            buffer->GetDecryptConfig()->key_id().end());
        params->access_units[index].iv = std::vector<char>(
            buffer->GetDecryptConfig()->iv().begin(),
            buffer->GetDecryptConfig()->iv().end());
        params->access_units[index].subsamples =
            buffer->GetDecryptConfig()->subsamples();
      }
      if (++index < params->access_units.size()) {
        ReadFromDemuxerStream(stream, params, index);
        return;
      }
      break;

    default:
      NOTREACHED();
  }

  if (proxy_)
    proxy_->ReadFromDemuxerAck(player_id_, *params);
  params->access_units.resize(0);
}

void MediaSourceDelegate::OnDemuxerError(
    media::PipelineStatus status) {
  DVLOG(1) << "MediaSourceDelegate::OnDemuxerError(" << status << ") : "
           << player_id_;
  if (status != media::PIPELINE_OK && !update_network_state_cb_.is_null())
    update_network_state_cb_.Run(PipelineErrorToNetworkState(status));
}

void MediaSourceDelegate::OnDemuxerInitDone(
    media::PipelineStatus status) {
  DVLOG(1) << "MediaSourceDelegate::OnDemuxerInitDone(" << status << ") : "
           << player_id_;
  if (status != media::PIPELINE_OK) {
    OnDemuxerError(status);
    return;
  }
  NotifyDemuxerReady("");
}

void MediaSourceDelegate::OnDemuxerStopDone() {
  DVLOG(1) << "MediaSourceDelegate::OnDemuxerStopDone() : " << player_id_;
  chunk_demuxer_.reset();
  delete this;
}

void MediaSourceDelegate::OnMediaConfigRequest() {
  NotifyDemuxerReady("");
}

void MediaSourceDelegate::NotifyDemuxerReady(const std::string& key_system) {
  if (!demuxer_)
    return;
  MediaPlayerHostMsg_DemuxerReady_Params params;
  DemuxerStream* audio_stream = demuxer_->GetStream(DemuxerStream::AUDIO);
  if (audio_stream) {
    const media::AudioDecoderConfig& config =
        audio_stream->audio_decoder_config();
    params.audio_codec = config.codec();
    params.audio_channels =
        media::ChannelLayoutToChannelCount(config.channel_layout());
    params.audio_sampling_rate = config.samples_per_second();
    params.is_audio_encrypted = config.is_encrypted();
    params.audio_extra_data = std::vector<uint8>(
        config.extra_data(), config.extra_data() + config.extra_data_size());
  }
  DemuxerStream* video_stream = demuxer_->GetStream(DemuxerStream::VIDEO);
  if (video_stream) {
    const media::VideoDecoderConfig& config =
        video_stream->video_decoder_config();
    params.video_codec = config.codec();
    params.video_size = config.natural_size();
    params.is_video_encrypted = config.is_encrypted();
    params.video_extra_data = std::vector<uint8>(
        config.extra_data(), config.extra_data() + config.extra_data_size());
  }
  params.duration_ms = GetDurationMs();
  params.key_system = key_system;

  bool ready_to_send = (!params.is_audio_encrypted &&
                        !params.is_video_encrypted) || !key_system.empty();
  if (proxy_ && ready_to_send)
    proxy_->DemuxerReady(player_id_, params);
}

int MediaSourceDelegate::GetDurationMs() {
  if (!chunk_demuxer_)
    return -1;

  double duration_ms = chunk_demuxer_->GetDuration() * 1000;
  if (duration_ms > std::numeric_limits<int>::max()) {
    LOG(WARNING) << "Duration from ChunkDemuxer is too large; probably "
                    "something has gone wrong.";
    return std::numeric_limits<int>::max();
  }
  return duration_ms;
}

void MediaSourceDelegate::OnDemuxerOpened() {
  if (!media_source_)
    return;

  media_source_->open(new WebMediaSourceClientImpl(
      chunk_demuxer_.get(), base::Bind(&LogMediaSourceError, media_log_)));
}

void MediaSourceDelegate::OnNeedKey(const std::string& key_system,
                                    const std::string& session_id,
                                    const std::string& type,
                                    scoped_ptr<uint8[]> init_data,
                                    int init_data_size) {
  if (need_key_cb_.is_null())
    return;

  need_key_cb_.Run(
      key_system, session_id, type, init_data.Pass(), init_data_size);
}

scoped_ptr<media::TextTrack> MediaSourceDelegate::OnAddTextTrack(
    media::TextKind kind,
    const std::string& label,
    const std::string& language) {
  return scoped_ptr<media::TextTrack>();
}

}  // namespace webkit_media