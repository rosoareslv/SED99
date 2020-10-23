/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.google.android.exoplayer;

import com.google.android.exoplayer.drm.DrmInitData;
import com.google.android.exoplayer.util.Assertions;
import com.google.android.exoplayer.util.MimeTypes;
import com.google.android.exoplayer.util.Util;

import android.annotation.TargetApi;
import android.content.Context;
import android.media.MediaExtractor;
import android.net.Uri;

import java.io.FileDescriptor;
import java.io.IOException;
import java.util.Map;
import java.util.UUID;

/**
 * Extracts samples from a stream using Android's {@link MediaExtractor}.
 */
// TODO: This implementation needs to be fixed so that its methods are non-blocking (either
// through use of a background thread, or through changes to the framework's MediaExtractor API).
@TargetApi(16)
public final class FrameworkSampleSource implements SampleSource {

  private static final int ALLOWED_FLAGS_MASK = C.SAMPLE_FLAG_SYNC | C.SAMPLE_FLAG_ENCRYPTED;

  private static final int TRACK_STATE_DISABLED = 0;
  private static final int TRACK_STATE_ENABLED = 1;
  private static final int TRACK_STATE_FORMAT_SENT = 2;

  // Parameters for a Uri data source.
  private final Context context;
  private final Uri uri;
  private final Map<String, String> headers;

  // Parameters for a FileDescriptor data source.
  private final FileDescriptor fileDescriptor;
  private final long fileDescriptorOffset;
  private final long fileDescriptorLength;

  private MediaExtractor extractor;
  private TrackInfo[] trackInfos;
  private boolean prepared;
  private int remainingReleaseCount;
  private int[] trackStates;
  private boolean[] pendingDiscontinuities;

  private long seekPositionUs;

  /**
   * Instantiates a new sample extractor reading from the specified {@code uri}.
   *
   * @param context Context for resolving {@code uri}.
   * @param uri The content URI from which to extract data.
   * @param headers Headers to send with requests for data.
   * @param downstreamRendererCount Number of track renderers dependent on this sample source.
   */
  public FrameworkSampleSource(Context context, Uri uri, Map<String, String> headers,
      int downstreamRendererCount) {
    Assertions.checkState(Util.SDK_INT >= 16);
    this.remainingReleaseCount = downstreamRendererCount;

    this.context = Assertions.checkNotNull(context);
    this.uri = Assertions.checkNotNull(uri);
    this.headers = headers;

    fileDescriptor = null;
    fileDescriptorOffset = 0;
    fileDescriptorLength = 0;
  }

  /**
   * Instantiates a new sample extractor reading from the specified seekable {@code fileDescriptor}.
   * The caller is responsible for releasing the file descriptor.
   *
   * @param fileDescriptor File descriptor from which to read.
   * @param offset The offset in bytes into the file where the data to be extracted starts.
   * @param length The length in bytes of the data to be extracted.
   * @param downstreamRendererCount Number of track renderers dependent on this sample source.
   */
  public FrameworkSampleSource(FileDescriptor fileDescriptor, long offset, long length,
      int downstreamRendererCount) {
    Assertions.checkState(Util.SDK_INT >= 16);
    this.remainingReleaseCount = downstreamRendererCount;

    context = null;
    uri = null;
    headers = null;

    this.fileDescriptor = Assertions.checkNotNull(fileDescriptor);
    fileDescriptorOffset = offset;
    fileDescriptorLength = length;
  }

  @Override
  public boolean prepare() throws IOException {
    if (!prepared) {
      extractor = new MediaExtractor();
      if (context != null) {
        extractor.setDataSource(context, uri, headers);
      } else {
        extractor.setDataSource(fileDescriptor, fileDescriptorOffset, fileDescriptorLength);
      }

      trackStates = new int[extractor.getTrackCount()];
      pendingDiscontinuities = new boolean[trackStates.length];
      trackInfos = new TrackInfo[trackStates.length];
      for (int i = 0; i < trackStates.length; i++) {
        android.media.MediaFormat format = extractor.getTrackFormat(i);
        long durationUs = format.containsKey(android.media.MediaFormat.KEY_DURATION)
            ? format.getLong(android.media.MediaFormat.KEY_DURATION) : C.UNKNOWN_TIME_US;
        String mime = format.getString(android.media.MediaFormat.KEY_MIME);
        trackInfos[i] = new TrackInfo(mime, durationUs);
      }
      prepared = true;
    }
    return true;
  }

  @Override
  public int getTrackCount() {
    Assertions.checkState(prepared);
    return trackStates.length;
  }

  @Override
  public TrackInfo getTrackInfo(int track) {
    Assertions.checkState(prepared);
    return trackInfos[track];
  }

  @Override
  public void enable(int track, long positionUs) {
    Assertions.checkState(prepared);
    Assertions.checkState(trackStates[track] == TRACK_STATE_DISABLED);
    trackStates[track] = TRACK_STATE_ENABLED;
    extractor.selectTrack(track);
    seekToUsInternal(positionUs, positionUs != 0);
  }

  @Override
  public boolean continueBuffering(long positionUs) {
    // MediaExtractor takes care of buffering and blocks until it has samples, so we can always
    // return true here. Although note that the blocking behavior is itself as bug, as per the
    // TODO further up this file. This method will need to return something else as part of fixing
    // the TODO.
    return true;
  }

  @Override
  public int readData(int track, long positionUs, MediaFormatHolder formatHolder,
      SampleHolder sampleHolder, boolean onlyReadDiscontinuity) {
    Assertions.checkState(prepared);
    Assertions.checkState(trackStates[track] != TRACK_STATE_DISABLED);
    if (pendingDiscontinuities[track]) {
      pendingDiscontinuities[track] = false;
      return DISCONTINUITY_READ;
    }
    if (onlyReadDiscontinuity) {
      return NOTHING_READ;
    }
    if (trackStates[track] != TRACK_STATE_FORMAT_SENT) {
      formatHolder.format = MediaFormat.createFromFrameworkMediaFormatV16(
          extractor.getTrackFormat(track));
      formatHolder.drmInitData = Util.SDK_INT >= 18 ? getDrmInitDataV18() : null;
      trackStates[track] = TRACK_STATE_FORMAT_SENT;
      return FORMAT_READ;
    }
    int extractorTrackIndex = extractor.getSampleTrackIndex();
    if (extractorTrackIndex == track) {
      if (sampleHolder.data != null) {
        int offset = sampleHolder.data.position();
        sampleHolder.size = extractor.readSampleData(sampleHolder.data, offset);
        sampleHolder.data.position(offset + sampleHolder.size);
      } else {
        sampleHolder.size = 0;
      }
      sampleHolder.timeUs = extractor.getSampleTime();
      sampleHolder.flags = extractor.getSampleFlags() & ALLOWED_FLAGS_MASK;
      if (sampleHolder.isEncrypted()) {
        sampleHolder.cryptoInfo.setFromExtractorV16(extractor);
      }
      seekPositionUs = C.UNKNOWN_TIME_US;
      extractor.advance();
      return SAMPLE_READ;
    } else {
      return extractorTrackIndex < 0 ? END_OF_STREAM : NOTHING_READ;
    }
  }

  @Override
  public void disable(int track) {
    Assertions.checkState(prepared);
    Assertions.checkState(trackStates[track] != TRACK_STATE_DISABLED);
    extractor.unselectTrack(track);
    pendingDiscontinuities[track] = false;
    trackStates[track] = TRACK_STATE_DISABLED;
  }

  @Override
  public void seekToUs(long positionUs) {
    Assertions.checkState(prepared);
    seekToUsInternal(positionUs, false);
  }

  @Override
  public long getBufferedPositionUs() {
    Assertions.checkState(prepared);
    long bufferedDurationUs = extractor.getCachedDuration();
    if (bufferedDurationUs == -1) {
      return TrackRenderer.UNKNOWN_TIME_US;
    } else {
      long sampleTime = extractor.getSampleTime();
      return sampleTime == -1 ? TrackRenderer.END_OF_TRACK_US : sampleTime + bufferedDurationUs;
    }
  }

  @Override
  public void release() {
    Assertions.checkState(remainingReleaseCount > 0);
    if (--remainingReleaseCount == 0) {
      extractor.release();
      extractor = null;
    }
  }

  @TargetApi(18)
  private DrmInitData getDrmInitDataV18() {
    // MediaExtractor only supports psshInfo for MP4, so it's ok to hard code the mimeType here.
    Map<UUID, byte[]> psshInfo = extractor.getPsshInfo();
    if (psshInfo == null || psshInfo.isEmpty()) {
      return null;
    }
    DrmInitData.Mapped drmInitData = new DrmInitData.Mapped(MimeTypes.VIDEO_MP4);
    drmInitData.putAll(psshInfo);
    return drmInitData;
  }

  private void seekToUsInternal(long positionUs, boolean force) {
    // Unless forced, avoid duplicate calls to the underlying extractor's seek method in the case
    // that there have been no interleaving calls to readSample.
    if (force || seekPositionUs != positionUs) {
      seekPositionUs = positionUs;
      extractor.seekTo(positionUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC);
      for (int i = 0; i < trackStates.length; ++i) {
        if (trackStates[i] != TRACK_STATE_DISABLED) {
          pendingDiscontinuities[i] = true;
        }
      }
    }
  }

}
