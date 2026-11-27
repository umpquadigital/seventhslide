#pragma once

// The script each web page runs (before its own) so that every sound it makes
// reaches SeventhSlide's mixer rather than the computer's speakers. The page
// itself is muted; this hears it anyway:
//
// - Web Audio: whatever a page connects to its speakers (an AudioContext's
//   destination) is connected to a capture node instead.
// - <audio> and <video>: each element that plays is tapped (captureStream),
//   leaving the element to the page; one the page routes through Web Audio
//   itself is left to the first rule.
// - Frames: a frame inside the page hands its sound to the top frame, which
//   alone can talk to the app.
//
// Each capture node (one per audio context) is a stream of its own, named by
// a random number, and hands 2048-frame chunks of 16-bit stereo, base64, to
// ``seventhslideSound.push(stream, rate, data)`` over Qt WebChannel; the app
// sums the streams. Sound from
// another site that does not allow it (CORS) is silence to Web Audio, and so
// to the mixer.

namespace seventhslide::app {

/// Prefixed with Qt WebChannel's own qwebchannel.js, inside one function so
/// neither leaves names in the page.
inline constexpr char k_web_page_sound_script[] = R"JS(
  if (window.__seventhslideSound) {
    return;
  }
  Object.defineProperty(window, '__seventhslideSound', { value: true });

  let send;
  if (window.top === window && typeof qt !== 'undefined' && qt.webChannelTransport) {
    let listener = null;
    new QWebChannel(qt.webChannelTransport, function (channel) {
      listener = channel.objects.seventhslideSound;
    });
    send = function (stream, rate, data) {
      if (listener) {
        listener.push(stream, rate, data);
      }
    };
    window.addEventListener('message', function (event) {
      const m = event.data && event.data.__seventhslideSound;
      if (m && typeof m.data === 'string') {
        send(m.stream | 0, m.rate | 0, m.data);
      }
    });
  } else {
    send = function (stream, rate, data) {
      window.top.postMessage({ __seventhslideSound: { stream: stream, rate: rate, data: data } }, '*');
    };
  }

  function encode(left, right) {
    const n = left.length;
    const pcm = new Int16Array(n * 2);
    for (let i = 0; i < n; ++i) {
      pcm[2 * i] = Math.max(-1, Math.min(1, left[i])) * 32767;
      pcm[2 * i + 1] = Math.max(-1, Math.min(1, right[i])) * 32767;
    }
    const bytes = new Uint8Array(pcm.buffer);
    let text = '';
    for (let i = 0; i < bytes.length; i += 0x8000) {
      text += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    }
    return btoa(text);
  }

  const connect = AudioNode.prototype.connect;
  const disconnect = AudioNode.prototype.disconnect;
  const captures = new WeakMap();

  // One capture node per context, kept quiet on the way to the (muted)
  // speakers: a script processor runs only while connected to them.
  function captureOf(context) {
    let node = captures.get(context);
    if (node) {
      return node;
    }
    node = context.createScriptProcessor(2048, 2, 2);
    // Unique across the page's frames, which each count their own.
    const stream = Math.floor(Math.random() * 0x7fffffff);
    const quiet = context.createGain();
    quiet.gain.value = 0;
    node.onaudioprocess = function (event) {
      const input = event.inputBuffer;
      const left = input.getChannelData(0);
      const right = input.numberOfChannels > 1 ? input.getChannelData(1) : left;
      send(stream, context.sampleRate, encode(left, right));
    };
    connect.call(node, quiet);
    connect.call(quiet, context.destination);
    captures.set(context, node);
    return node;
  }

  // An offline context renders to a buffer, not the speakers: left alone.
  function heard(node, target) {
    return target instanceof AudioDestinationNode && node.context instanceof AudioContext;
  }
  AudioNode.prototype.connect = function (target, ...rest) {
    return heard(this, target) ? connect.call(this, captureOf(this.context), ...rest)
                               : connect.call(this, target, ...rest);
  };
  AudioNode.prototype.disconnect = function (target, ...rest) {
    return heard(this, target) ? disconnect.call(this, captureOf(this.context), ...rest)
                               : disconnect.apply(this, arguments);
  };

  // Media elements: tapped while the page owns them.
  let mediaContext = null;
  const taps = new WeakMap();
  const routedByPage = new WeakSet();
  const createMediaElementSource = AudioContext.prototype.createMediaElementSource;
  AudioContext.prototype.createMediaElementSource = function (element) {
    const tap = taps.get(element);
    if (tap) {
      disconnect.call(tap.node);
      taps.delete(element);
    }
    routedByPage.add(element);
    return createMediaElementSource.call(this, element);
  };
  function tap(event) {
    const element = event.target;
    if (!(element instanceof HTMLMediaElement) || routedByPage.has(element)) {
      return;
    }
    try {
      let tap = taps.get(element);
      if (!tap) {
        tap = { stream: element.captureStream(), node: null, track: null };
        taps.set(element, tap);
      }
      // A new source on the element brings a new track.
      const track = tap.stream.getAudioTracks()[0] || null;
      if (track === tap.track) {
        return;
      }
      if (tap.node) {
        disconnect.call(tap.node);
        tap.node = null;
      }
      tap.track = track;
      if (track) {
        mediaContext = mediaContext || new AudioContext({ sampleRate: 48000 });
        tap.node = mediaContext.createMediaStreamSource(new MediaStream([track]));
        connect.call(tap.node, captureOf(mediaContext));
      }
    } catch (error) {
      // Another site's media (no CORS): Web Audio may not hear it.
    }
  }
  document.addEventListener('play', tap, true);
  document.addEventListener('playing', tap, true);
)JS";

} // namespace seventhslide::app
