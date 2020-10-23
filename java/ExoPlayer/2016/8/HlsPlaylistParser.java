/*
 * Copyright (C) 2016 The Android Open Source Project
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
package com.google.android.exoplayer2.source.hls.playlist;

import android.net.Uri;
import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.Format;
import com.google.android.exoplayer2.ParserException;
import com.google.android.exoplayer2.source.hls.playlist.HlsMediaPlaylist.Segment;
import com.google.android.exoplayer2.upstream.ParsingLoadable;
import com.google.android.exoplayer2.util.MimeTypes;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedList;
import java.util.List;
import java.util.Queue;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * HLS playlists parsing logic.
 */
public final class HlsPlaylistParser implements ParsingLoadable.Parser<HlsPlaylist> {

  private static final String TAG_VERSION = "#EXT-X-VERSION";
  private static final String TAG_STREAM_INF = "#EXT-X-STREAM-INF";
  private static final String TAG_MEDIA = "#EXT-X-MEDIA";
  private static final String TAG_DISCONTINUITY = "#EXT-X-DISCONTINUITY";
  private static final String TAG_DISCONTINUITY_SEQUENCE = "#EXT-X-DISCONTINUITY-SEQUENCE";
  private static final String TAG_MEDIA_DURATION = "#EXTINF";
  private static final String TAG_MEDIA_SEQUENCE = "#EXT-X-MEDIA-SEQUENCE";
  private static final String TAG_TARGET_DURATION = "#EXT-X-TARGETDURATION";
  private static final String TAG_ENDLIST = "#EXT-X-ENDLIST";
  private static final String TAG_KEY = "#EXT-X-KEY";
  private static final String TAG_BYTERANGE = "#EXT-X-BYTERANGE";

  private static final String TYPE_AUDIO = "AUDIO";
  private static final String TYPE_VIDEO = "VIDEO";
  private static final String TYPE_SUBTITLES = "SUBTITLES";
  private static final String TYPE_CLOSED_CAPTIONS = "CLOSED-CAPTIONS";

  private static final String METHOD_NONE = "NONE";
  private static final String METHOD_AES128 = "AES-128";

  private static final String BOOLEAN_TRUE = "YES";
  private static final String BOOLEAN_FALSE = "NO";

  private static final Pattern REGEX_BANDWIDTH = Pattern.compile("BANDWIDTH=(\\d+)\\b");
  private static final Pattern REGEX_CODECS = Pattern.compile("CODECS=\"(.+?)\"");
  private static final Pattern REGEX_RESOLUTION = Pattern.compile("RESOLUTION=(\\d+x\\d+)");
  private static final Pattern REGEX_VERSION = Pattern.compile(TAG_VERSION + ":(\\d+)\\b");
  private static final Pattern REGEX_TARGET_DURATION = Pattern.compile(TAG_TARGET_DURATION
      + ":(\\d+)\\b");
  private static final Pattern REGEX_MEDIA_SEQUENCE = Pattern.compile(TAG_MEDIA_SEQUENCE
      + ":(\\d+)\\b");
  private static final Pattern REGEX_MEDIA_DURATION = Pattern.compile(TAG_MEDIA_DURATION
      + ":([\\d\\.]+)\\b");
  private static final Pattern REGEX_BYTERANGE = Pattern.compile(TAG_BYTERANGE
      + ":(\\d+(?:@\\d+)?)\\b");
  private static final Pattern REGEX_METHOD = Pattern.compile("METHOD=(" + METHOD_NONE + "|"
      + METHOD_AES128 + ")");
  private static final Pattern REGEX_URI = Pattern.compile("URI=\"(.+?)\"");
  private static final Pattern REGEX_IV = Pattern.compile("IV=([^,.*]+)");
  private static final Pattern REGEX_TYPE = Pattern.compile("TYPE=(" + TYPE_AUDIO + "|" + TYPE_VIDEO
      + "|" + TYPE_SUBTITLES + "|" + TYPE_CLOSED_CAPTIONS + ")");
  private static final Pattern REGEX_LANGUAGE = Pattern.compile("LANGUAGE=\"(.+?)\"");
  private static final Pattern REGEX_NAME = Pattern.compile("NAME=\"(.+?)\"");
  private static final Pattern REGEX_INSTREAM_ID = Pattern.compile("INSTREAM-ID=\"(.+?)\"");
  private static final Pattern REGEX_AUTOSELECT = compileBooleanAttrPattern("AUTOSELECT");
  private static final Pattern REGEX_DEFAULT = compileBooleanAttrPattern("DEFAULT");
  private static final Pattern REGEX_FORCED = compileBooleanAttrPattern("FORCED");

  @Override
  public HlsPlaylist parse(Uri uri, InputStream inputStream) throws IOException {
    BufferedReader reader = new BufferedReader(new InputStreamReader(inputStream));
    Queue<String> extraLines = new LinkedList<>();
    String line;
    try {
      while ((line = reader.readLine()) != null) {
        line = line.trim();
        if (line.isEmpty()) {
          // Do nothing.
        } else if (line.startsWith(TAG_STREAM_INF)) {
          extraLines.add(line);
          return parseMasterPlaylist(new LineIterator(extraLines, reader), uri.toString());
        } else if (line.startsWith(TAG_TARGET_DURATION)
            || line.startsWith(TAG_MEDIA_SEQUENCE)
            || line.startsWith(TAG_MEDIA_DURATION)
            || line.startsWith(TAG_KEY)
            || line.startsWith(TAG_BYTERANGE)
            || line.equals(TAG_DISCONTINUITY)
            || line.equals(TAG_DISCONTINUITY_SEQUENCE)
            || line.equals(TAG_ENDLIST)) {
          extraLines.add(line);
          return parseMediaPlaylist(new LineIterator(extraLines, reader), uri.toString());
        } else {
          extraLines.add(line);
        }
      }
    } finally {
      reader.close();
    }
    throw new ParserException("Failed to parse the playlist, could not identify any tags.");
  }

  private static HlsMasterPlaylist parseMasterPlaylist(LineIterator iterator, String baseUri)
      throws IOException {
    ArrayList<Variant> variants = new ArrayList<>();
    ArrayList<Variant> audios = new ArrayList<>();
    ArrayList<Variant> subtitles = new ArrayList<>();
    int bitrate = 0;
    String codecs = null;
    int width = Format.NO_VALUE;
    int height = Format.NO_VALUE;
    String name = null;
    Format muxedAudioFormat = null;
    Format muxedCaptionFormat = null;

    boolean expectingStreamInfUrl = false;
    String line;
    while (iterator.hasNext()) {
      line = iterator.next();
      if (line.startsWith(TAG_MEDIA)) {
        boolean isDefault = parseBooleanAttribute(line, REGEX_DEFAULT, false);
        boolean isForced = parseBooleanAttribute(line, REGEX_FORCED, false);
        boolean isAutoselect = parseBooleanAttribute(line, REGEX_AUTOSELECT,
            false);
        int selectionFlags = (isDefault ? Format.SELECTION_FLAG_DEFAULT : 0)
            | (isForced ? Format.SELECTION_FLAG_FORCED : 0)
            | (isAutoselect ? Format.SELECTION_FLAG_AUTOSELECT : 0);
        String type = parseStringAttr(line, REGEX_TYPE);
        if (TYPE_CLOSED_CAPTIONS.equals(type)) {
          String instreamId = parseStringAttr(line, REGEX_INSTREAM_ID);
          if ("CC1".equals(instreamId)) {
            // We assume all subtitles belong to the same group.
            String captionName = parseStringAttr(line, REGEX_NAME);
            String language = parseOptionalStringAttr(line, REGEX_LANGUAGE);
            muxedCaptionFormat = Format.createTextContainerFormat(captionName,
                MimeTypes.APPLICATION_M3U8, MimeTypes.APPLICATION_EIA608, null, Format.NO_VALUE,
                selectionFlags, language);
          }
        } else if (TYPE_SUBTITLES.equals(type)) {
          // We assume all subtitles belong to the same group.
          String subtitleName = parseStringAttr(line, REGEX_NAME);
          String uri = parseStringAttr(line, REGEX_URI);
          String language = parseOptionalStringAttr(line, REGEX_LANGUAGE);
          Format format = Format.createTextContainerFormat(subtitleName, MimeTypes.APPLICATION_M3U8,
              MimeTypes.TEXT_VTT, null, bitrate, selectionFlags, language);
          subtitles.add(new Variant(uri, format, codecs));
        } else if (TYPE_AUDIO.equals(type)) {
          // We assume all audios belong to the same group.
          String uri = parseOptionalStringAttr(line, REGEX_URI);
          String language = parseOptionalStringAttr(line, REGEX_LANGUAGE);
          String audioName = parseStringAttr(line, REGEX_NAME);
          int audioBitrate = uri != null ? bitrate : Format.NO_VALUE;
          Format format = Format.createAudioContainerFormat(audioName, MimeTypes.APPLICATION_M3U8,
              null, null, audioBitrate, Format.NO_VALUE, Format.NO_VALUE, null, selectionFlags,
              language);
          if (uri != null) {
            audios.add(new Variant(uri, format, codecs));
          } else {
            muxedAudioFormat = format;
          }
        }
      } else if (line.startsWith(TAG_STREAM_INF)) {
        bitrate = parseIntAttr(line, REGEX_BANDWIDTH);
        codecs = parseOptionalStringAttr(line, REGEX_CODECS);
        name = parseOptionalStringAttr(line, REGEX_NAME);
        String resolutionString = parseOptionalStringAttr(line, REGEX_RESOLUTION);
        if (resolutionString != null) {
          String[] widthAndHeight = resolutionString.split("x");
          width = Integer.parseInt(widthAndHeight[0]);
          if (width <= 0) {
            // Width was invalid.
            width = Format.NO_VALUE;
          }
          height = Integer.parseInt(widthAndHeight[1]);
          if (height <= 0) {
            // Height was invalid.
            height = Format.NO_VALUE;
          }
        } else {
          width = Format.NO_VALUE;
          height = Format.NO_VALUE;
        }
        expectingStreamInfUrl = true;
      } else if (!line.startsWith("#") && expectingStreamInfUrl) {
        if (name == null) {
          name = Integer.toString(variants.size());
        }
        Format format = Format.createVideoContainerFormat(name, MimeTypes.APPLICATION_M3U8, null,
            null, bitrate, width, height, Format.NO_VALUE, null);
        variants.add(new Variant(line, format, codecs));
        bitrate = 0;
        codecs = null;
        name = null;
        width = Format.NO_VALUE;
        height = Format.NO_VALUE;
        expectingStreamInfUrl = false;
      }
    }
    return new HlsMasterPlaylist(baseUri, variants, audios, subtitles, muxedAudioFormat,
        muxedCaptionFormat);
  }

  private static HlsMediaPlaylist parseMediaPlaylist(LineIterator iterator, String baseUri)
      throws IOException {
    int mediaSequence = 0;
    int targetDurationSecs = 0;
    int version = 1; // Default version == 1.
    boolean live = true;
    List<Segment> segments = new ArrayList<>();

    double segmentDurationSecs = 0.0;
    int discontinuitySequenceNumber = 0;
    long segmentStartTimeUs = 0;
    long segmentByterangeOffset = 0;
    long segmentByterangeLength = C.LENGTH_UNSET;
    int segmentMediaSequence = 0;

    boolean isEncrypted = false;
    String encryptionKeyUri = null;
    String encryptionIV = null;

    String line;
    while (iterator.hasNext()) {
      line = iterator.next();
      if (line.startsWith(TAG_TARGET_DURATION)) {
        targetDurationSecs = parseIntAttr(line, REGEX_TARGET_DURATION);
      } else if (line.startsWith(TAG_MEDIA_SEQUENCE)) {
        mediaSequence = parseIntAttr(line, REGEX_MEDIA_SEQUENCE);
        segmentMediaSequence = mediaSequence;
      } else if (line.startsWith(TAG_VERSION)) {
        version = parseIntAttr(line, REGEX_VERSION);
      } else if (line.startsWith(TAG_MEDIA_DURATION)) {
        segmentDurationSecs = parseDoubleAttr(line, REGEX_MEDIA_DURATION);
      } else if (line.startsWith(TAG_KEY)) {
        String method = parseStringAttr(line, REGEX_METHOD);
        isEncrypted = METHOD_AES128.equals(method);
        if (isEncrypted) {
          encryptionKeyUri = parseStringAttr(line, REGEX_URI);
          encryptionIV = parseOptionalStringAttr(line, REGEX_IV);
        } else {
          encryptionKeyUri = null;
          encryptionIV = null;
        }
      } else if (line.startsWith(TAG_BYTERANGE)) {
        String byteRange = parseStringAttr(line, REGEX_BYTERANGE);
        String[] splitByteRange = byteRange.split("@");
        segmentByterangeLength = Long.parseLong(splitByteRange[0]);
        if (splitByteRange.length > 1) {
          segmentByterangeOffset = Long.parseLong(splitByteRange[1]);
        }
      } else if (line.startsWith(TAG_DISCONTINUITY_SEQUENCE)) {
        discontinuitySequenceNumber = Integer.parseInt(line.substring(line.indexOf(':') + 1));
      } else if (line.equals(TAG_DISCONTINUITY)) {
        discontinuitySequenceNumber++;
      } else if (!line.startsWith("#")) {
        String segmentEncryptionIV;
        if (!isEncrypted) {
          segmentEncryptionIV = null;
        } else if (encryptionIV != null) {
          segmentEncryptionIV = encryptionIV;
        } else {
          segmentEncryptionIV = Integer.toHexString(segmentMediaSequence);
        }
        segmentMediaSequence++;
        if (segmentByterangeLength == C.LENGTH_UNSET) {
          segmentByterangeOffset = 0;
        }
        segments.add(new Segment(line, segmentDurationSecs, discontinuitySequenceNumber,
            segmentStartTimeUs, isEncrypted, encryptionKeyUri, segmentEncryptionIV,
            segmentByterangeOffset, segmentByterangeLength));
        segmentStartTimeUs += (long) (segmentDurationSecs * C.MICROS_PER_SECOND);
        segmentDurationSecs = 0.0;
        if (segmentByterangeLength != C.LENGTH_UNSET) {
          segmentByterangeOffset += segmentByterangeLength;
        }
        segmentByterangeLength = C.LENGTH_UNSET;
      } else if (line.equals(TAG_ENDLIST)) {
        live = false;
      }
    }
    return new HlsMediaPlaylist(baseUri, mediaSequence, targetDurationSecs, version, live,
        Collections.unmodifiableList(segments));
  }

  private static String parseStringAttr(String line, Pattern pattern) throws ParserException {
    Matcher matcher = pattern.matcher(line);
    if (matcher.find() && matcher.groupCount() == 1) {
      return matcher.group(1);
    }
    throw new ParserException("Couldn't match " + pattern.pattern() + " in " + line);
  }

  private static int parseIntAttr(String line, Pattern pattern) throws ParserException {
    return Integer.parseInt(parseStringAttr(line, pattern));
  }

  private static double parseDoubleAttr(String line, Pattern pattern) throws ParserException {
    return Double.parseDouble(parseStringAttr(line, pattern));
  }

  private static String parseOptionalStringAttr(String line, Pattern pattern) {
    Matcher matcher = pattern.matcher(line);
    if (matcher.find()) {
      return matcher.group(1);
    }
    return null;
  }

  private static boolean parseBooleanAttribute(String line, Pattern pattern, boolean defaultValue) {
    Matcher matcher = pattern.matcher(line);
    if (matcher.find()) {
      return matcher.group(1).equals(BOOLEAN_TRUE);
    }
    return defaultValue;
  }

  private static Pattern compileBooleanAttrPattern(String attribute) {
    return Pattern.compile(attribute + "=(" + BOOLEAN_FALSE + "|" + BOOLEAN_TRUE + ")");
  }

  private static class LineIterator {

    private final BufferedReader reader;
    private final Queue<String> extraLines;

    private String next;

    public LineIterator(Queue<String> extraLines, BufferedReader reader) {
      this.extraLines = extraLines;
      this.reader = reader;
    }

    public boolean hasNext() throws IOException {
      if (next != null) {
        return true;
      }
      if (!extraLines.isEmpty()) {
        next = extraLines.poll();
        return true;
      }
      while ((next = reader.readLine()) != null) {
        next = next.trim();
        if (!next.isEmpty()) {
          return true;
        }
      }
      return false;
    }

    public String next() throws IOException {
      String result = null;
      if (hasNext()) {
        result = next;
        next = null;
      }
      return result;
    }

  }

}
