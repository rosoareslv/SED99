/*
 * Copyright (C) 2017 The Android Open Source Project
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
package com.google.android.exoplayer2.ext.mediasession;

import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ResultReceiver;
import android.os.SystemClock;
import android.support.annotation.LongDef;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;
import android.support.v4.media.MediaDescriptionCompat;
import android.support.v4.media.MediaMetadataCompat;
import android.support.v4.media.RatingCompat;
import android.support.v4.media.session.MediaControllerCompat;
import android.support.v4.media.session.MediaSessionCompat;
import android.support.v4.media.session.PlaybackStateCompat;
import android.util.Pair;
import com.google.android.exoplayer2.C;
import com.google.android.exoplayer2.ControlDispatcher;
import com.google.android.exoplayer2.DefaultControlDispatcher;
import com.google.android.exoplayer2.ExoPlaybackException;
import com.google.android.exoplayer2.ExoPlayerLibraryInfo;
import com.google.android.exoplayer2.PlaybackParameters;
import com.google.android.exoplayer2.Player;
import com.google.android.exoplayer2.Timeline;
import com.google.android.exoplayer2.util.Assertions;
import com.google.android.exoplayer2.util.ErrorMessageProvider;
import com.google.android.exoplayer2.util.RepeatModeUtil;
import com.google.android.exoplayer2.util.Util;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Connects a {@link MediaSessionCompat} to a {@link Player}.
 *
 * <p>This connector does <em>not</em> call {@link MediaSessionCompat#setActive(boolean)}, and so
 * application code is responsible for making the session active when desired. A session must be
 * active for transport controls to be displayed (e.g. on the lock screen) and for it to receive
 * media button events.
 *
 * <p>The connector listens for actions sent by the media session's controller and implements these
 * actions by calling appropriate player methods. The playback state of the media session is
 * automatically synced with the player. The connector can also be optionally extended by providing
 * various collaborators:
 *
 * <ul>
 *   <li>Actions to initiate media playback ({@code PlaybackStateCompat#ACTION_PREPARE_*} and {@code
 *       PlaybackStateCompat#ACTION_PLAY_*}) can be handled by a {@link PlaybackPreparer} passed
 *       when calling {@link #setPlayer(Player, PlaybackPreparer, CustomActionProvider...)}. Custom
 *       actions can be handled by passing one or more {@link CustomActionProvider}s in a similar
 *       way.
 *   <li>To enable a media queue and navigation within it, you can set a {@link QueueNavigator} by
 *       calling {@link #setQueueNavigator(QueueNavigator)}. Use of {@link TimelineQueueNavigator}
 *       is recommended for most use cases.
 *   <li>To enable editing of the media queue, you can set a {@link QueueEditor} by calling {@link
 *       #setQueueEditor(QueueEditor)}.
 *   <li>An {@link ErrorMessageProvider} for providing human readable error messages and
 *       corresponding error codes can be set by calling {@link
 *       #setErrorMessageProvider(ErrorMessageProvider)}.
 * </ul>
 */
public final class MediaSessionConnector {

  static {
    ExoPlayerLibraryInfo.registerModule("goog.exo.mediasession");
  }

  /** Playback actions supported by the connector. */
  @LongDef(
      flag = true,
      value = {
        PlaybackStateCompat.ACTION_PLAY_PAUSE,
        PlaybackStateCompat.ACTION_PLAY,
        PlaybackStateCompat.ACTION_PAUSE,
        PlaybackStateCompat.ACTION_SEEK_TO,
        PlaybackStateCompat.ACTION_FAST_FORWARD,
        PlaybackStateCompat.ACTION_REWIND,
        PlaybackStateCompat.ACTION_STOP,
        PlaybackStateCompat.ACTION_SET_REPEAT_MODE,
        PlaybackStateCompat.ACTION_SET_SHUFFLE_MODE
      })
  @Retention(RetentionPolicy.SOURCE)
  public @interface PlaybackActions {}

  @PlaybackActions
  public static final long ALL_PLAYBACK_ACTIONS =
      PlaybackStateCompat.ACTION_PLAY_PAUSE
          | PlaybackStateCompat.ACTION_PLAY
          | PlaybackStateCompat.ACTION_PAUSE
          | PlaybackStateCompat.ACTION_SEEK_TO
          | PlaybackStateCompat.ACTION_FAST_FORWARD
          | PlaybackStateCompat.ACTION_REWIND
          | PlaybackStateCompat.ACTION_STOP
          | PlaybackStateCompat.ACTION_SET_REPEAT_MODE
          | PlaybackStateCompat.ACTION_SET_SHUFFLE_MODE;

  /** The default playback actions. */
  @PlaybackActions public static final long DEFAULT_PLAYBACK_ACTIONS = ALL_PLAYBACK_ACTIONS;

  /** The default fast forward increment, in milliseconds. */
  public static final int DEFAULT_FAST_FORWARD_MS = 15000;
  /** The default rewind increment, in milliseconds. */
  public static final int DEFAULT_REWIND_MS = 5000;

  public static final String EXTRAS_PITCH = "EXO_PITCH";

  private static final long BASE_PLAYBACK_ACTIONS =
      PlaybackStateCompat.ACTION_PLAY_PAUSE
          | PlaybackStateCompat.ACTION_PLAY
          | PlaybackStateCompat.ACTION_PAUSE
          | PlaybackStateCompat.ACTION_STOP
          | PlaybackStateCompat.ACTION_SET_SHUFFLE_MODE
          | PlaybackStateCompat.ACTION_SET_REPEAT_MODE;
  private static final int BASE_MEDIA_SESSION_FLAGS =
      MediaSessionCompat.FLAG_HANDLES_MEDIA_BUTTONS
          | MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS;
  private static final int EDITOR_MEDIA_SESSION_FLAGS =
      BASE_MEDIA_SESSION_FLAGS | MediaSessionCompat.FLAG_HANDLES_QUEUE_COMMANDS;

  /** Receiver of media commands sent by a media controller. */
  public interface CommandReceiver {
    /**
     * See {@link MediaSessionCompat.Callback#onCommand(String, Bundle, ResultReceiver)}. The
     * receiver may handle the command, but is not required to do so. Changes to the player should
     * be made via the {@link ControlDispatcher}.
     *
     * @param player The player connected to the media session.
     * @param controlDispatcher A {@link ControlDispatcher} that should be used for dispatching
     *     changes to the player.
     * @param command The command name.
     * @param extras Optional parameters for the command, may be null.
     * @param cb A result receiver to which a result may be sent by the command, may be null.
     * @return Whether the receiver handled the command.
     */
    boolean onCommand(
        Player player,
        ControlDispatcher controlDispatcher,
        String command,
        Bundle extras,
        ResultReceiver cb);
  }

  /** Interface to which playback preparation actions are delegated. */
  public interface PlaybackPreparer extends CommandReceiver {

    long ACTIONS =
        PlaybackStateCompat.ACTION_PREPARE
            | PlaybackStateCompat.ACTION_PREPARE_FROM_MEDIA_ID
            | PlaybackStateCompat.ACTION_PREPARE_FROM_SEARCH
            | PlaybackStateCompat.ACTION_PREPARE_FROM_URI
            | PlaybackStateCompat.ACTION_PLAY_FROM_MEDIA_ID
            | PlaybackStateCompat.ACTION_PLAY_FROM_SEARCH
            | PlaybackStateCompat.ACTION_PLAY_FROM_URI;

    /**
     * Returns the actions which are supported by the preparer. The supported actions must be a
     * bitmask combined out of {@link PlaybackStateCompat#ACTION_PREPARE}, {@link
     * PlaybackStateCompat#ACTION_PREPARE_FROM_MEDIA_ID}, {@link
     * PlaybackStateCompat#ACTION_PREPARE_FROM_SEARCH}, {@link
     * PlaybackStateCompat#ACTION_PREPARE_FROM_URI}, {@link
     * PlaybackStateCompat#ACTION_PLAY_FROM_MEDIA_ID}, {@link
     * PlaybackStateCompat#ACTION_PLAY_FROM_SEARCH} and {@link
     * PlaybackStateCompat#ACTION_PLAY_FROM_URI}.
     *
     * @return The bitmask of the supported media actions.
     */
    long getSupportedPrepareActions();
    /** See {@link MediaSessionCompat.Callback#onPrepare()}. */
    void onPrepare();
    /** See {@link MediaSessionCompat.Callback#onPrepareFromMediaId(String, Bundle)}. */
    void onPrepareFromMediaId(String mediaId, Bundle extras);
    /** See {@link MediaSessionCompat.Callback#onPrepareFromSearch(String, Bundle)}. */
    void onPrepareFromSearch(String query, Bundle extras);
    /** See {@link MediaSessionCompat.Callback#onPrepareFromUri(Uri, Bundle)}. */
    void onPrepareFromUri(Uri uri, Bundle extras);
  }

  /**
   * Handles queue navigation actions, and updates the media session queue by calling {@code
   * MediaSessionCompat.setQueue()}.
   */
  public interface QueueNavigator extends CommandReceiver {

    long ACTIONS =
        PlaybackStateCompat.ACTION_SKIP_TO_QUEUE_ITEM
            | PlaybackStateCompat.ACTION_SKIP_TO_NEXT
            | PlaybackStateCompat.ACTION_SKIP_TO_PREVIOUS;

    /**
     * Returns the actions which are supported by the navigator. The supported actions must be a
     * bitmask combined out of {@link PlaybackStateCompat#ACTION_SKIP_TO_QUEUE_ITEM}, {@link
     * PlaybackStateCompat#ACTION_SKIP_TO_NEXT}, {@link
     * PlaybackStateCompat#ACTION_SKIP_TO_PREVIOUS}.
     *
     * @param player The player connected to the media session.
     * @return The bitmask of the supported media actions.
     */
    long getSupportedQueueNavigatorActions(Player player);
    /**
     * Called when the timeline of the player has changed.
     *
     * @param player The player connected to the media session.
     */
    void onTimelineChanged(Player player);
    /**
     * Called when the current window index changed.
     *
     * @param player The player connected to the media session.
     */
    void onCurrentWindowIndexChanged(Player player);
    /**
     * Gets the id of the currently active queue item, or {@link
     * MediaSessionCompat.QueueItem#UNKNOWN_ID} if the active item is unknown.
     *
     * <p>To let the connector publish metadata for the active queue item, the queue item with the
     * returned id must be available in the list of items returned by {@link
     * MediaControllerCompat#getQueue()}.
     *
     * @param player The player connected to the media session.
     * @return The id of the active queue item.
     */
    long getActiveQueueItemId(@Nullable Player player);
    /**
     * See {@link MediaSessionCompat.Callback#onSkipToPrevious()}.
     *
     * @param player The player connected to the media session.
     * @param controlDispatcher A {@link ControlDispatcher} that should be used for dispatching
     *     changes to the player.
     */
    void onSkipToPrevious(Player player, ControlDispatcher controlDispatcher);
    /**
     * See {@link MediaSessionCompat.Callback#onSkipToQueueItem(long)}.
     *
     * @param player The player connected to the media session.
     * @param controlDispatcher A {@link ControlDispatcher} that should be used for dispatching
     *     changes to the player.
     */
    void onSkipToQueueItem(Player player, ControlDispatcher controlDispatcher, long id);
    /**
     * See {@link MediaSessionCompat.Callback#onSkipToNext()}.
     *
     * @param player The player connected to the media session.
     * @param controlDispatcher A {@link ControlDispatcher} that should be used for dispatching
     *     changes to the player.
     */
    void onSkipToNext(Player player, ControlDispatcher controlDispatcher);
  }

  /** Handles media session queue edits. */
  public interface QueueEditor extends CommandReceiver {

    /**
     * See {@link MediaSessionCompat.Callback#onAddQueueItem(MediaDescriptionCompat description)}.
     */
    void onAddQueueItem(Player player, MediaDescriptionCompat description);
    /**
     * See {@link MediaSessionCompat.Callback#onAddQueueItem(MediaDescriptionCompat description, int
     * index)}.
     */
    void onAddQueueItem(Player player, MediaDescriptionCompat description, int index);
    /**
     * See {@link MediaSessionCompat.Callback#onRemoveQueueItem(MediaDescriptionCompat
     * description)}.
     */
    void onRemoveQueueItem(Player player, MediaDescriptionCompat description);
  }

  /** Callback receiving a user rating for the active media item. */
  public interface RatingCallback extends CommandReceiver {

    /** See {@link MediaSessionCompat.Callback#onSetRating(RatingCompat)}. */
    void onSetRating(Player player, RatingCompat rating);

    /** See {@link MediaSessionCompat.Callback#onSetRating(RatingCompat, Bundle)}. */
    void onSetRating(Player player, RatingCompat rating, Bundle extras);
  }

  /**
   * Provides a {@link PlaybackStateCompat.CustomAction} to be published and handles the action when
   * sent by a media controller.
   */
  public interface CustomActionProvider {
    /**
     * Called when a custom action provided by this provider is sent to the media session.
     *
     * @param player The player connected to the media session.
     * @param controlDispatcher A {@link ControlDispatcher} that should be used for dispatching
     *     changes to the player.
     * @param action The name of the action which was sent by a media controller.
     * @param extras Optional extras sent by a media controller.
     */
    void onCustomAction(
        Player player, ControlDispatcher controlDispatcher, String action, Bundle extras);

    /**
     * Returns a {@link PlaybackStateCompat.CustomAction} which will be published to the media
     * session by the connector or {@code null} if this action should not be published at the given
     * player state.
     *
     * @param player The player connected to the media session.
     * @return The custom action to be included in the session playback state or {@code null}.
     */
    PlaybackStateCompat.CustomAction getCustomAction(Player player);
  }

  /** Provides a {@link MediaMetadataCompat} for a given player state. */
  public interface MediaMetadataProvider {
    /**
     * Gets the {@link MediaMetadataCompat} to be published to the session.
     *
     * @param player The player connected to the media session.
     * @return The {@link MediaMetadataCompat} to be published to the session.
     */
    MediaMetadataCompat getMetadata(Player player);
  }

  /** The wrapped {@link MediaSessionCompat}. */
  public final MediaSessionCompat mediaSession;

  @Nullable private final MediaMetadataProvider mediaMetadataProvider;
  private final ExoPlayerEventListener exoPlayerEventListener;
  private final MediaSessionCallback mediaSessionCallback;
  private final ArrayList<CommandReceiver> commandReceivers;

  private Player player;
  private ControlDispatcher controlDispatcher;
  private CustomActionProvider[] customActionProviders;
  private Map<String, CustomActionProvider> customActionMap;
  @Nullable private ErrorMessageProvider<? super ExoPlaybackException> errorMessageProvider;
  @Nullable private Pair<Integer, CharSequence> customError;
  private PlaybackPreparer playbackPreparer;
  private QueueNavigator queueNavigator;
  private QueueEditor queueEditor;
  private RatingCallback ratingCallback;

  private long enabledPlaybackActions;
  private int rewindMs;
  private int fastForwardMs;

  /**
   * Creates an instance.
   *
   * <p>Equivalent to {@code MediaSessionConnector(mediaSession, new
   * DefaultMediaMetadataProvider(mediaSession.getController(), null))}.
   *
   * @param mediaSession The {@link MediaSessionCompat} to connect to.
   */
  public MediaSessionConnector(MediaSessionCompat mediaSession) {
    this(
        mediaSession,
        new DefaultMediaMetadataProvider(mediaSession.getController(), null));
  }

  /**
   * Creates an instance.
   *
   * @param mediaSession The {@link MediaSessionCompat} to connect to.
   * @param mediaMetadataProvider A {@link MediaMetadataProvider} for providing a custom metadata
   *     object to be published to the media session, or {@code null} if metadata shouldn't be
   *     published.
   */
  public MediaSessionConnector(
      MediaSessionCompat mediaSession,
      @Nullable MediaMetadataProvider mediaMetadataProvider) {
    this.mediaSession = mediaSession;
    this.mediaMetadataProvider = mediaMetadataProvider;
    mediaSession.setFlags(BASE_MEDIA_SESSION_FLAGS);
    mediaSessionCallback = new MediaSessionCallback();
    exoPlayerEventListener = new ExoPlayerEventListener();
    controlDispatcher = new DefaultControlDispatcher();
    customActionMap = Collections.emptyMap();
    commandReceivers = new ArrayList<>();
    enabledPlaybackActions = DEFAULT_PLAYBACK_ACTIONS;
    rewindMs = DEFAULT_REWIND_MS;
    fastForwardMs = DEFAULT_FAST_FORWARD_MS;
  }

  /**
   * Sets the player to be connected to the media session. Must be called on the same thread that is
   * used to access the player.
   *
   * <p>The order in which any {@link CustomActionProvider}s are passed determines the order of the
   * actions published with the playback state of the session.
   *
   * @param player The player to be connected to the {@code MediaSession}, or {@code null} to
   *     disconnect the current player.
   * @param playbackPreparer An optional {@link PlaybackPreparer} for preparing the player.
   * @param customActionProviders Optional {@link CustomActionProvider}s to publish and handle
   *     custom actions.
   */
  public void setPlayer(
      @Nullable Player player,
      @Nullable PlaybackPreparer playbackPreparer,
      CustomActionProvider... customActionProviders) {
    Assertions.checkArgument(player == null || player.getApplicationLooper() == Looper.myLooper());
    if (this.player != null) {
      this.player.removeListener(exoPlayerEventListener);
      mediaSession.setCallback(null);
    }

    unregisterCommandReceiver(this.playbackPreparer);
    this.player = player;
    this.playbackPreparer = playbackPreparer;
    registerCommandReceiver(playbackPreparer);

    this.customActionProviders =
        (player != null && customActionProviders != null)
            ? customActionProviders
            : new CustomActionProvider[0];
    if (player != null) {
      Handler handler = new Handler(Util.getLooper());
      mediaSession.setCallback(mediaSessionCallback, handler);
      player.addListener(exoPlayerEventListener);
    }
    invalidateMediaSessionPlaybackState();
    invalidateMediaSessionMetadata();
  }

  /**
   * Sets the {@link ControlDispatcher}.
   *
   * @param controlDispatcher The {@link ControlDispatcher}, or null to use {@link
   *     DefaultControlDispatcher}.
   */
  public void setControlDispatcher(@Nullable ControlDispatcher controlDispatcher) {
    if (this.controlDispatcher != controlDispatcher) {
      this.controlDispatcher =
          controlDispatcher == null ? new DefaultControlDispatcher() : controlDispatcher;
    }
  }

  /**
   * Sets the enabled playback actions.
   *
   * @param enabledPlaybackActions The enabled playback actions.
   */
  public void setEnabledPlaybackActions(@PlaybackActions long enabledPlaybackActions) {
    enabledPlaybackActions &= ALL_PLAYBACK_ACTIONS;
    if (this.enabledPlaybackActions != enabledPlaybackActions) {
      this.enabledPlaybackActions = enabledPlaybackActions;
      invalidateMediaSessionPlaybackState();
    }
  }

  /**
   * Sets the rewind increment in milliseconds.
   *
   * @param rewindMs The rewind increment in milliseconds. A non-positive value will cause the
   *     rewind button to be disabled.
   */
  public void setRewindIncrementMs(int rewindMs) {
    if (this.rewindMs != rewindMs) {
      this.rewindMs = rewindMs;
      invalidateMediaSessionPlaybackState();
    }
  }

  /**
   * Sets the fast forward increment in milliseconds.
   *
   * @param fastForwardMs The fast forward increment in milliseconds. A non-positive value will
   *     cause the fast forward button to be disabled.
   */
  public void setFastForwardIncrementMs(int fastForwardMs) {
    if (this.fastForwardMs != fastForwardMs) {
      this.fastForwardMs = fastForwardMs;
      invalidateMediaSessionPlaybackState();
    }
  }

  /**
   * Sets the optional {@link ErrorMessageProvider}.
   *
   * @param errorMessageProvider The error message provider.
   */
  public void setErrorMessageProvider(
      @Nullable ErrorMessageProvider<? super ExoPlaybackException> errorMessageProvider) {
    if (this.errorMessageProvider != errorMessageProvider) {
      this.errorMessageProvider = errorMessageProvider;
      invalidateMediaSessionPlaybackState();
    }
  }

  /**
   * Sets the {@link QueueNavigator} to handle queue navigation actions {@code ACTION_SKIP_TO_NEXT},
   * {@code ACTION_SKIP_TO_PREVIOUS} and {@code ACTION_SKIP_TO_QUEUE_ITEM}.
   *
   * @param queueNavigator The queue navigator.
   */
  public void setQueueNavigator(QueueNavigator queueNavigator) {
    if (this.queueNavigator != queueNavigator) {
      unregisterCommandReceiver(this.queueNavigator);
      this.queueNavigator = queueNavigator;
      registerCommandReceiver(queueNavigator);
    }
  }

  /**
   * Sets the {@link QueueEditor} to handle queue edits sent by the media controller.
   *
   * @param queueEditor The queue editor.
   */
  public void setQueueEditor(QueueEditor queueEditor) {
    if (this.queueEditor != queueEditor) {
      unregisterCommandReceiver(this.queueEditor);
      this.queueEditor = queueEditor;
      registerCommandReceiver(queueEditor);
      mediaSession.setFlags(
          queueEditor == null ? BASE_MEDIA_SESSION_FLAGS : EDITOR_MEDIA_SESSION_FLAGS);
    }
  }

  /**
   * Sets the {@link RatingCallback} to handle user ratings.
   *
   * @param ratingCallback The rating callback.
   */
  public void setRatingCallback(RatingCallback ratingCallback) {
    if (this.ratingCallback != ratingCallback) {
      unregisterCommandReceiver(this.ratingCallback);
      this.ratingCallback = ratingCallback;
      registerCommandReceiver(this.ratingCallback);
    }
  }

  /**
   * Sets a custom error on the session.
   *
   * <p>This sets the error code via {@link PlaybackStateCompat.Builder#setErrorMessage(int,
   * CharSequence)}. By default, the error code will be set to {@link
   * PlaybackStateCompat#ERROR_CODE_APP_ERROR}.
   *
   * @param message The error string to report or {@code null} to clear the error.
   */
  public void setCustomErrorMessage(@Nullable CharSequence message) {
    int code = (message == null) ? 0 : PlaybackStateCompat.ERROR_CODE_APP_ERROR;
    setCustomErrorMessage(message, code);
  }

  /**
   * Sets a custom error on the session.
   *
   * @param message The error string to report or {@code null} to clear the error.
   * @param code The error code to report. Ignored when {@code message} is {@code null}.
   */
  public void setCustomErrorMessage(@Nullable CharSequence message, int code) {
    customError = (message == null) ? null : new Pair<>(code, message);
    invalidateMediaSessionPlaybackState();
  }

  /**
   * Updates the metadata of the media session.
   *
   * <p>Apps normally only need to call this method when the backing data for a given media item has
   * changed and the metadata should be updated immediately.
   */
  public final void invalidateMediaSessionMetadata() {
    if (mediaMetadataProvider != null && player != null) {
      mediaSession.setMetadata(mediaMetadataProvider.getMetadata(player));
    }
  }

  /**
   * Updates the playback state of the media session.
   *
   * <p>Apps normally only need to call this method when the custom actions provided by a {@link
   * CustomActionProvider} changed and the playback state needs to be updated immediately.
   */
  public final void invalidateMediaSessionPlaybackState() {
    PlaybackStateCompat.Builder builder = new PlaybackStateCompat.Builder();
    if (player == null) {
      builder.setActions(/* capabilities= */ 0).setState(PlaybackStateCompat.STATE_NONE, 0, 0, 0);
      mediaSession.setPlaybackState(builder.build());
      return;
    }

    Map<String, CustomActionProvider> currentActions = new HashMap<>();
    for (CustomActionProvider customActionProvider : customActionProviders) {
      PlaybackStateCompat.CustomAction customAction = customActionProvider.getCustomAction(player);
      if (customAction != null) {
        currentActions.put(customAction.getAction(), customActionProvider);
        builder.addCustomAction(customAction);
      }
    }
    customActionMap = Collections.unmodifiableMap(currentActions);

    int playbackState = player.getPlaybackState();
    ExoPlaybackException playbackError =
        playbackState == Player.STATE_IDLE ? player.getPlaybackError() : null;
    boolean reportError = playbackError != null || customError != null;
    int sessionPlaybackState =
        reportError
            ? PlaybackStateCompat.STATE_ERROR
            : mapPlaybackState(player.getPlaybackState(), player.getPlayWhenReady());
    if (customError != null) {
      builder.setErrorMessage(customError.first, customError.second);
    } else if (playbackError != null && errorMessageProvider != null) {
      Pair<Integer, String> message = errorMessageProvider.getErrorMessage(playbackError);
      builder.setErrorMessage(message.first, message.second);
    }
    long activeQueueItemId =
        queueNavigator != null
            ? queueNavigator.getActiveQueueItemId(player)
            : MediaSessionCompat.QueueItem.UNKNOWN_ID;
    Bundle extras = new Bundle();
    extras.putFloat(EXTRAS_PITCH, player.getPlaybackParameters().pitch);
    builder
        .setActions(buildPlaybackActions(player))
        .setActiveQueueItemId(activeQueueItemId)
        .setBufferedPosition(player.getBufferedPosition())
        .setState(
            sessionPlaybackState,
            player.getCurrentPosition(),
            player.getPlaybackParameters().speed,
            SystemClock.elapsedRealtime())
        .setExtras(extras);
    mediaSession.setPlaybackState(builder.build());
  }

  /**
   * Updates the queue of the media session by calling {@link
   * QueueNavigator#onTimelineChanged(Player)}.
   *
   * <p>Apps normally only need to call this method when the backing data for a given queue item has
   * changed and the queue should be updated immediately.
   */
  public final void invalidateMediaSessionQueue() {
    if (queueNavigator != null && player != null) {
      queueNavigator.onTimelineChanged(player);
    }
  }

  private void registerCommandReceiver(CommandReceiver commandReceiver) {
    if (!commandReceivers.contains(commandReceiver)) {
      commandReceivers.add(commandReceiver);
    }
  }

  private void unregisterCommandReceiver(CommandReceiver commandReceiver) {
    commandReceivers.remove(commandReceiver);
  }

  private long buildPlaybackActions(Player player) {
    boolean enableSeeking = false;
    boolean enableRewind = false;
    boolean enableFastForward = false;
    boolean enableSetRating = false;
    Timeline timeline = player.getCurrentTimeline();
    if (!timeline.isEmpty() && !player.isPlayingAd()) {
      enableSeeking = player.isCurrentWindowSeekable();
      enableRewind = enableSeeking && rewindMs > 0;
      enableFastForward = enableSeeking && fastForwardMs > 0;
      enableSetRating = true;
    }

    long playbackActions = BASE_PLAYBACK_ACTIONS;
    if (enableSeeking) {
      playbackActions |= PlaybackStateCompat.ACTION_SEEK_TO;
    }
    if (enableFastForward) {
      playbackActions |= PlaybackStateCompat.ACTION_FAST_FORWARD;
    }
    if (enableRewind) {
      playbackActions |= PlaybackStateCompat.ACTION_REWIND;
    }
    playbackActions &= enabledPlaybackActions;

    long actions = playbackActions;
    if (playbackPreparer != null) {
      actions |= (PlaybackPreparer.ACTIONS & playbackPreparer.getSupportedPrepareActions());
    }
    if (queueNavigator != null) {
      actions |=
          (QueueNavigator.ACTIONS & queueNavigator.getSupportedQueueNavigatorActions(player));
    }
    if (ratingCallback != null && enableSetRating) {
      actions |= PlaybackStateCompat.ACTION_SET_RATING;
    }
    return actions;
  }

  private int mapPlaybackState(int exoPlayerPlaybackState, boolean playWhenReady) {
    switch (exoPlayerPlaybackState) {
      case Player.STATE_BUFFERING:
        return PlaybackStateCompat.STATE_BUFFERING;
      case Player.STATE_READY:
        return playWhenReady ? PlaybackStateCompat.STATE_PLAYING : PlaybackStateCompat.STATE_PAUSED;
      case Player.STATE_ENDED:
        return PlaybackStateCompat.STATE_STOPPED;
      default:
        return PlaybackStateCompat.STATE_NONE;
    }
  }

  private boolean canDispatchPlaybackAction(long action) {
    return player != null && (enabledPlaybackActions & action) != 0;
  }

  private boolean canDispatchToPlaybackPreparer(long action) {
    return player != null
        && playbackPreparer != null
        && (playbackPreparer.getSupportedPrepareActions() & action) != 0;
  }

  private boolean canDispatchToQueueNavigator(long action) {
    return player != null
        && queueNavigator != null
        && (queueNavigator.getSupportedQueueNavigatorActions(player) & action) != 0;
  }

  private boolean canDispatchSetRating() {
    return player != null && ratingCallback != null;
  }

  private boolean canDispatchQueueEdit() {
    return player != null && queueEditor != null;
  }

  private void rewind(Player player) {
    if (player.isCurrentWindowSeekable() && rewindMs > 0) {
      seekTo(player, player.getCurrentPosition() - rewindMs);
    }
  }

  private void fastForward(Player player) {
    if (player.isCurrentWindowSeekable() && fastForwardMs > 0) {
      seekTo(player, player.getCurrentPosition() + fastForwardMs);
    }
  }

  private void seekTo(Player player, long positionMs) {
    seekTo(player, player.getCurrentWindowIndex(), positionMs);
  }

  private void seekTo(Player player, int windowIndex, long positionMs) {
    long durationMs = player.getDuration();
    if (durationMs != C.TIME_UNSET) {
      positionMs = Math.min(positionMs, durationMs);
    }
    positionMs = Math.max(positionMs, 0);
    controlDispatcher.dispatchSeekTo(player, windowIndex, positionMs);
  }

  /**
   * Provides a default {@link MediaMetadataCompat} with properties and extras propagated from the
   * active queue item to the session metadata.
   */
  public static final class DefaultMediaMetadataProvider implements MediaMetadataProvider {

    private final MediaControllerCompat mediaController;
    private final String metadataExtrasPrefix;

    /**
     * Creates a new instance.
     *
     * @param mediaController The {@link MediaControllerCompat}.
     * @param metadataExtrasPrefix A string to prefix extra keys which are propagated from the
     *     active queue item to the session metadata.
     */
    public DefaultMediaMetadataProvider(
        MediaControllerCompat mediaController, @Nullable String metadataExtrasPrefix) {
      this.mediaController = mediaController;
      this.metadataExtrasPrefix = metadataExtrasPrefix != null ? metadataExtrasPrefix : "";
    }

    @Override
    public MediaMetadataCompat getMetadata(Player player) {
      if (player.getCurrentTimeline().isEmpty()) {
        return null;
      }
      MediaMetadataCompat.Builder builder = new MediaMetadataCompat.Builder();
      if (player.isPlayingAd()) {
        builder.putLong(MediaMetadataCompat.METADATA_KEY_ADVERTISEMENT, 1);
      }
      builder.putLong(
          MediaMetadataCompat.METADATA_KEY_DURATION,
          player.getDuration() == C.TIME_UNSET ? -1 : player.getDuration());
      long activeQueueItemId = mediaController.getPlaybackState().getActiveQueueItemId();
      if (activeQueueItemId != MediaSessionCompat.QueueItem.UNKNOWN_ID) {
        List<MediaSessionCompat.QueueItem> queue = mediaController.getQueue();
        for (int i = 0; queue != null && i < queue.size(); i++) {
          MediaSessionCompat.QueueItem queueItem = queue.get(i);
          if (queueItem.getQueueId() == activeQueueItemId) {
            MediaDescriptionCompat description = queueItem.getDescription();
            Bundle extras = description.getExtras();
            if (extras != null) {
              for (String key : extras.keySet()) {
                Object value = extras.get(key);
                if (value instanceof String) {
                  builder.putString(metadataExtrasPrefix + key, (String) value);
                } else if (value instanceof CharSequence) {
                  builder.putText(metadataExtrasPrefix + key, (CharSequence) value);
                } else if (value instanceof Long) {
                  builder.putLong(metadataExtrasPrefix + key, (Long) value);
                } else if (value instanceof Integer) {
                  builder.putLong(metadataExtrasPrefix + key, (Integer) value);
                } else if (value instanceof Bitmap) {
                  builder.putBitmap(metadataExtrasPrefix + key, (Bitmap) value);
                } else if (value instanceof RatingCompat) {
                  builder.putRating(metadataExtrasPrefix + key, (RatingCompat) value);
                }
              }
            }
            if (description.getTitle() != null) {
              String title = String.valueOf(description.getTitle());
              builder.putString(MediaMetadataCompat.METADATA_KEY_TITLE, title);
              builder.putString(MediaMetadataCompat.METADATA_KEY_DISPLAY_TITLE, title);
            }
            if (description.getSubtitle() != null) {
              builder.putString(
                  MediaMetadataCompat.METADATA_KEY_DISPLAY_SUBTITLE,
                  String.valueOf(description.getSubtitle()));
            }
            if (description.getDescription() != null) {
              builder.putString(
                  MediaMetadataCompat.METADATA_KEY_DISPLAY_DESCRIPTION,
                  String.valueOf(description.getDescription()));
            }
            if (description.getIconBitmap() != null) {
              builder.putBitmap(
                  MediaMetadataCompat.METADATA_KEY_DISPLAY_ICON, description.getIconBitmap());
            }
            if (description.getIconUri() != null) {
              builder.putString(
                  MediaMetadataCompat.METADATA_KEY_DISPLAY_ICON_URI,
                  String.valueOf(description.getIconUri()));
            }
            if (description.getMediaId() != null) {
              builder.putString(
                  MediaMetadataCompat.METADATA_KEY_MEDIA_ID,
                  String.valueOf(description.getMediaId()));
            }
            if (description.getMediaUri() != null) {
              builder.putString(
                  MediaMetadataCompat.METADATA_KEY_MEDIA_URI,
                  String.valueOf(description.getMediaUri()));
            }
            break;
          }
        }
      }
      return builder.build();
    }
  }

  private class ExoPlayerEventListener implements Player.EventListener {

    private int currentWindowIndex;
    private int currentWindowCount;

    @Override
    public void onTimelineChanged(
        Timeline timeline, @Nullable Object manifest, @Player.TimelineChangeReason int reason) {
      int windowCount = player.getCurrentTimeline().getWindowCount();
      int windowIndex = player.getCurrentWindowIndex();
      if (queueNavigator != null) {
        queueNavigator.onTimelineChanged(player);
        invalidateMediaSessionPlaybackState();
      } else if (currentWindowCount != windowCount || currentWindowIndex != windowIndex) {
        // active queue item and queue navigation actions may need to be updated
        invalidateMediaSessionPlaybackState();
      }
      currentWindowCount = windowCount;
      currentWindowIndex = windowIndex;
      invalidateMediaSessionMetadata();
    }

    @Override
    public void onPlayerStateChanged(boolean playWhenReady, int playbackState) {
      invalidateMediaSessionPlaybackState();
    }

    @Override
    public void onRepeatModeChanged(@Player.RepeatMode int repeatMode) {
      mediaSession.setRepeatMode(
          repeatMode == Player.REPEAT_MODE_ONE
              ? PlaybackStateCompat.REPEAT_MODE_ONE
              : repeatMode == Player.REPEAT_MODE_ALL
                  ? PlaybackStateCompat.REPEAT_MODE_ALL
                  : PlaybackStateCompat.REPEAT_MODE_NONE);
      invalidateMediaSessionPlaybackState();
    }

    @Override
    public void onShuffleModeEnabledChanged(boolean shuffleModeEnabled) {
      mediaSession.setShuffleMode(
          shuffleModeEnabled
              ? PlaybackStateCompat.SHUFFLE_MODE_ALL
              : PlaybackStateCompat.SHUFFLE_MODE_NONE);
      invalidateMediaSessionPlaybackState();
    }

    @Override
    public void onPositionDiscontinuity(@Player.DiscontinuityReason int reason) {
      if (currentWindowIndex != player.getCurrentWindowIndex()) {
        if (queueNavigator != null) {
          queueNavigator.onCurrentWindowIndexChanged(player);
        }
        currentWindowIndex = player.getCurrentWindowIndex();
        // Update playback state after queueNavigator.onCurrentWindowIndexChanged has been called
        // and before updating metadata.
        invalidateMediaSessionPlaybackState();
        invalidateMediaSessionMetadata();
        return;
      }
      invalidateMediaSessionPlaybackState();
    }

    @Override
    public void onPlaybackParametersChanged(PlaybackParameters playbackParameters) {
      invalidateMediaSessionPlaybackState();
    }
  }

  private class MediaSessionCallback extends MediaSessionCompat.Callback {

    @Override
    public void onPlay() {
      if (canDispatchPlaybackAction(PlaybackStateCompat.ACTION_PLAY)) {
        if (player.getPlaybackState() == Player.STATE_IDLE) {
          if (playbackPreparer != null) {
            playbackPreparer.onPrepare();
          }
        } else if (player.getPlaybackState() == Player.STATE_ENDED) {
          controlDispatcher.dispatchSeekTo(player, player.getCurrentWindowIndex(), C.TIME_UNSET);
        }
        controlDispatcher.dispatchSetPlayWhenReady(player, /* playWhenReady= */ true);
      }
    }

    @Override
    public void onPause() {
      if (canDispatchPlaybackAction(PlaybackStateCompat.ACTION_PAUSE)) {
        controlDispatcher.dispatchSetPlayWhenReady(player, /* playWhenReady= */ false);
      }
    }

    @Override
    public void onSeekTo(long positionMs) {
      if (canDispatchPlaybackAction(PlaybackStateCompat.ACTION_SEEK_TO)) {
        seekTo(player, positionMs);
      }
    }

    @Override
    public void onFastForward() {
      if (canDispatchPlaybackAction(PlaybackStateCompat.ACTION_FAST_FORWARD)) {
        fastForward(player);
      }
    }

    @Override
    public void onRewind() {
      if (canDispatchPlaybackAction(PlaybackStateCompat.ACTION_REWIND)) {
        rewind(player);
      }
    }

    @Override
    public void onStop() {
      if (canDispatchPlaybackAction(PlaybackStateCompat.ACTION_STOP)) {
        controlDispatcher.dispatchStop(player, /* reset= */ true);
      }
    }

    @Override
    public void onSetShuffleMode(int shuffleMode) {
      if (canDispatchPlaybackAction(PlaybackStateCompat.ACTION_SET_SHUFFLE_MODE)) {
        boolean shuffleModeEnabled =
            shuffleMode == PlaybackStateCompat.SHUFFLE_MODE_ALL
                || shuffleMode == PlaybackStateCompat.SHUFFLE_MODE_GROUP;
        controlDispatcher.dispatchSetShuffleModeEnabled(player, shuffleModeEnabled);
      }
    }

    @Override
    public void onSetRepeatMode(int mediaSessionRepeatMode) {
      if (canDispatchPlaybackAction(PlaybackStateCompat.ACTION_SET_REPEAT_MODE)) {
        @RepeatModeUtil.RepeatToggleModes int repeatMode;
        switch (mediaSessionRepeatMode) {
          case PlaybackStateCompat.REPEAT_MODE_ALL:
          case PlaybackStateCompat.REPEAT_MODE_GROUP:
            repeatMode = Player.REPEAT_MODE_ALL;
            break;
          case PlaybackStateCompat.REPEAT_MODE_ONE:
            repeatMode = Player.REPEAT_MODE_ONE;
            break;
          default:
            repeatMode = Player.REPEAT_MODE_OFF;
            break;
        }
        controlDispatcher.dispatchSetRepeatMode(player, repeatMode);
      }
    }

    @Override
    public void onSkipToNext() {
      if (canDispatchToQueueNavigator(PlaybackStateCompat.ACTION_SKIP_TO_NEXT)) {
        queueNavigator.onSkipToNext(player, controlDispatcher);
      }
    }

    @Override
    public void onSkipToPrevious() {
      if (canDispatchToQueueNavigator(PlaybackStateCompat.ACTION_SKIP_TO_PREVIOUS)) {
        queueNavigator.onSkipToPrevious(player, controlDispatcher);
      }
    }

    @Override
    public void onSkipToQueueItem(long id) {
      if (canDispatchToQueueNavigator(PlaybackStateCompat.ACTION_SKIP_TO_QUEUE_ITEM)) {
        queueNavigator.onSkipToQueueItem(player, controlDispatcher, id);
      }
    }

    @Override
    public void onCustomAction(@NonNull String action, @Nullable Bundle extras) {
      if (player != null && customActionMap.containsKey(action)) {
        customActionMap.get(action).onCustomAction(player, controlDispatcher, action, extras);
        invalidateMediaSessionPlaybackState();
      }
    }

    @Override
    public void onCommand(String command, Bundle extras, ResultReceiver cb) {
      if (player != null) {
        for (int i = 0; i < commandReceivers.size(); i++) {
          if (commandReceivers.get(i).onCommand(player, controlDispatcher, command, extras, cb)) {
            return;
          }
        }
      }
    }

    @Override
    public void onPrepare() {
      if (canDispatchToPlaybackPreparer(PlaybackStateCompat.ACTION_PREPARE)) {
        player.stop();
        player.setPlayWhenReady(false);
        playbackPreparer.onPrepare();
      }
    }

    @Override
    public void onPrepareFromMediaId(String mediaId, Bundle extras) {
      if (canDispatchToPlaybackPreparer(PlaybackStateCompat.ACTION_PREPARE_FROM_MEDIA_ID)) {
        player.stop();
        player.setPlayWhenReady(false);
        playbackPreparer.onPrepareFromMediaId(mediaId, extras);
      }
    }

    @Override
    public void onPrepareFromSearch(String query, Bundle extras) {
      if (canDispatchToPlaybackPreparer(PlaybackStateCompat.ACTION_PREPARE_FROM_SEARCH)) {
        player.stop();
        player.setPlayWhenReady(false);
        playbackPreparer.onPrepareFromSearch(query, extras);
      }
    }

    @Override
    public void onPrepareFromUri(Uri uri, Bundle extras) {
      if (canDispatchToPlaybackPreparer(PlaybackStateCompat.ACTION_PREPARE_FROM_URI)) {
        player.stop();
        player.setPlayWhenReady(false);
        playbackPreparer.onPrepareFromUri(uri, extras);
      }
    }

    @Override
    public void onPlayFromMediaId(String mediaId, Bundle extras) {
      if (canDispatchToPlaybackPreparer(PlaybackStateCompat.ACTION_PLAY_FROM_MEDIA_ID)) {
        player.stop();
        player.setPlayWhenReady(true);
        playbackPreparer.onPrepareFromMediaId(mediaId, extras);
      }
    }

    @Override
    public void onPlayFromSearch(String query, Bundle extras) {
      if (canDispatchToPlaybackPreparer(PlaybackStateCompat.ACTION_PLAY_FROM_SEARCH)) {
        player.stop();
        player.setPlayWhenReady(true);
        playbackPreparer.onPrepareFromSearch(query, extras);
      }
    }

    @Override
    public void onPlayFromUri(Uri uri, Bundle extras) {
      if (canDispatchToPlaybackPreparer(PlaybackStateCompat.ACTION_PLAY_FROM_URI)) {
        player.stop();
        player.setPlayWhenReady(true);
        playbackPreparer.onPrepareFromUri(uri, extras);
      }
    }

    @Override
    public void onSetRating(RatingCompat rating) {
      if (canDispatchSetRating()) {
        ratingCallback.onSetRating(player, rating);
      }
    }

    @Override
    public void onSetRating(RatingCompat rating, Bundle extras) {
      if (canDispatchSetRating()) {
        ratingCallback.onSetRating(player, rating, extras);
      }
    }

    @Override
    public void onAddQueueItem(MediaDescriptionCompat description) {
      if (canDispatchQueueEdit()) {
        queueEditor.onAddQueueItem(player, description);
      }
    }

    @Override
    public void onAddQueueItem(MediaDescriptionCompat description, int index) {
      if (canDispatchQueueEdit()) {
        queueEditor.onAddQueueItem(player, description, index);
      }
    }

    @Override
    public void onRemoveQueueItem(MediaDescriptionCompat description) {
      if (canDispatchQueueEdit()) {
        queueEditor.onRemoveQueueItem(player, description);
      }
    }
  }
}
