/*
 * Copyright 2002-2015 the original author or authors.
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

package org.springframework.core.codec.support;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.ByteBufUtil;
import io.netty.buffer.Unpooled;
import org.reactivestreams.Publisher;
import reactor.Publishers;
import reactor.fn.Function;

import org.springframework.core.ResolvableType;
import org.springframework.util.MimeType;

/**
 * Decode an arbitrary split byte stream representing JSON objects to a byte
 * stream where each chunk is a well-formed JSON object.
 *
 * This class does not do any real parsing or validation. A sequence of byte
 * is considered a JSON object/array if it contains a matching number of opening
 * and closing braces/brackets.
 *
 * Based on <a href=https://github.com/netty/netty/blob/master/codec/src/main/java/io/netty/handler/codec/json/JsonObjectDecoder.java">
 * Netty {@code JsonObjectDecoder}</a>
 *
 * @author Sebastien Deleuze
 * @see JsonObjectEncoder
 */
public class JsonObjectDecoder extends AbstractDecoder<ByteBuffer> {

	private static final int ST_CORRUPTED = -1;

	private static final int ST_INIT = 0;

	private static final int ST_DECODING_NORMAL = 1;

	private static final int ST_DECODING_ARRAY_STREAM = 2;


	private final int maxObjectLength;

	private final boolean streamArrayElements;


	public JsonObjectDecoder() {
		// 1 MB
		this(1024 * 1024);
	}

	public JsonObjectDecoder(int maxObjectLength) {
		this(maxObjectLength, true);
	}

	public JsonObjectDecoder(boolean streamArrayElements) {
		this(1024 * 1024, streamArrayElements);
	}


	/**
	 * @param maxObjectLength maximum number of bytes a JSON object/array may
	 * use (including braces and all). Objects exceeding this length are dropped
	 * and an {@link IllegalStateException} is thrown.
	 * @param streamArrayElements if set to true and the "top level" JSON object
	 * is an array, each of its entries is passed through the pipeline individually
	 * and immediately after it was fully received, allowing for arrays with
	 * "infinitely" many elements.
	 */
	public JsonObjectDecoder(int maxObjectLength, boolean streamArrayElements) {
		super(new MimeType("application", "json", StandardCharsets.UTF_8),
				new MimeType("application", "*+json", StandardCharsets.UTF_8));
		if (maxObjectLength < 1) {
			throw new IllegalArgumentException("maxObjectLength must be a positive int");
		}
		this.maxObjectLength = maxObjectLength;
		this.streamArrayElements = streamArrayElements;
	}

	@Override
	public Publisher<ByteBuffer> decode(Publisher<ByteBuffer> inputStream, ResolvableType type,
			MimeType mimeType, Object... hints) {

		return Publishers.flatMap(inputStream, new Function<ByteBuffer, Publisher<? extends ByteBuffer>>() {

			int openBraces;
			int index;
			int state;
			boolean insideString;
			ByteBuf input;
			Integer writerIndex;

			@Override
			public Publisher<? extends ByteBuffer> apply(ByteBuffer b) {
				List<ByteBuffer> chunks = new ArrayList<>();
				if (this.input == null) {
					this.input = Unpooled.copiedBuffer(b);
					this.writerIndex = this.input.writerIndex();
				}
				else {
					this.input = Unpooled.copiedBuffer(this.input, Unpooled.copiedBuffer(b));
					this.writerIndex = this.input.writerIndex();
				}
				if (this.state == ST_CORRUPTED) {
					this.input.skipBytes(this.input.readableBytes());
					return Publishers.error(new IllegalStateException("Corrupted stream"));
				}
				if (this.writerIndex > maxObjectLength) {
					// buffer size exceeded maxObjectLength; discarding the complete buffer.
					this.input.skipBytes(this.input.readableBytes());
					reset();
					return Publishers.error(new IllegalStateException("object length exceeds " +
							maxObjectLength + ": " + this.writerIndex + " bytes discarded"));
				}
				for (/* use current index */; this.index < this.writerIndex; this.index++) {
					byte c = this.input.getByte(this.index);
					if (this.state == ST_DECODING_NORMAL) {
						decodeByte(c, this.input, this.index);

						// All opening braces/brackets have been closed. That's enough to conclude
						// that the JSON object/array is complete.
						if (this.openBraces == 0) {
							ByteBuf json = extractObject(this.input, this.input.readerIndex(),
									this.index + 1 - this.input.readerIndex());
							if (json != null) {
								chunks.add(json.nioBuffer());
							}

							// The JSON object/array was extracted => discard the bytes from
							// the input buffer.
							this.input.readerIndex(this.index + 1);
							// Reset the object state to get ready for the next JSON object/text
							// coming along the byte stream.
							reset();
						}
					}
					else if (this.state == ST_DECODING_ARRAY_STREAM) {
						decodeByte(c, this.input, this.index);

						if (!this.insideString && (this.openBraces == 1 && c == ',' ||
								this.openBraces == 0 && c == ']')) {
							// skip leading spaces. No range check is needed and the loop will terminate
							// because the byte at position index is not a whitespace.
							for (int i = this.input.readerIndex(); Character.isWhitespace(this.input.getByte(i)); i++) {
								this.input.skipBytes(1);
							}

							// skip trailing spaces.
							int idxNoSpaces = this.index - 1;
							while (idxNoSpaces >= this.input.readerIndex() &&
									Character.isWhitespace(this.input.getByte(idxNoSpaces))) {

								idxNoSpaces--;
							}

							ByteBuf json = extractObject(this.input, this.input.readerIndex(),
									idxNoSpaces + 1 - this.input.readerIndex());

							if (json != null) {
								chunks.add(json.nioBuffer());
							}

							this.input.readerIndex(this.index + 1);

							if (c == ']') {
								reset();
							}
						}
						// JSON object/array detected. Accumulate bytes until all braces/brackets are closed.
					}
					else if (c == '{' || c == '[') {
						initDecoding(c, streamArrayElements);

						if (this.state == ST_DECODING_ARRAY_STREAM) {
							// Discard the array bracket
							this.input.skipBytes(1);
						}
						// Discard leading spaces in front of a JSON object/array.
					}
					else if (Character.isWhitespace(c)) {
						this.input.skipBytes(1);
					}
					else {
						this.state = ST_CORRUPTED;
						return Publishers.error(new IllegalStateException(
								"invalid JSON received at byte position " + this.index + ": " +
										ByteBufUtil.hexDump(this.input)));
					}
				}

				if (this.input.readableBytes() == 0) {
					this.index = 0;
				}
				return Publishers.from(chunks);
			}

			/**
			 * Override this method if you want to filter the json objects/arrays that
			 * get passed through the pipeline.
			 */
			@SuppressWarnings("UnusedParameters")
			protected ByteBuf extractObject(ByteBuf buffer, int index, int length) {
				return buffer.slice(index, length).retain();
			}

			private void decodeByte(byte c, ByteBuf input, int index) {
				if ((c == '{' || c == '[') && !this.insideString) {
					this.openBraces++;
				}
				else if ((c == '}' || c == ']') && !this.insideString) {
					this.openBraces--;
				}
				else if (c == '"') {
					// start of a new JSON string. It's necessary to detect strings as they may
					// also contain braces/brackets and that could lead to incorrect results.
					if (!this.insideString) {
						this.insideString = true;
						// If the double quote wasn't escaped then this is the end of a string.
					}
					else if (input.getByte(index - 1) != '\\') {
						this.insideString = false;
					}
				}
			}

			private void initDecoding(byte openingBrace, boolean streamArrayElements) {
				this.openBraces = 1;
				if (openingBrace == '[' && streamArrayElements) {
					this.state = ST_DECODING_ARRAY_STREAM;
				}
				else {
					this.state = ST_DECODING_NORMAL;
				}
			}

			private void reset() {
				this.insideString = false;
				this.state = ST_INIT;
				this.openBraces = 0;
			}
		});
	}

}
