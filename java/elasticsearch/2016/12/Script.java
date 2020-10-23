/*
 * Licensed to Elasticsearch under one or more contributor
 * license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright
 * ownership. Elasticsearch licenses this file to you under
 * the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package org.elasticsearch.script;

import org.elasticsearch.Version;
import org.elasticsearch.common.ParseField;
import org.elasticsearch.common.ParseFieldMatcher;
import org.elasticsearch.common.ParseFieldMatcherSupplier;
import org.elasticsearch.common.bytes.BytesArray;
import org.elasticsearch.common.io.stream.StreamInput;
import org.elasticsearch.common.io.stream.StreamOutput;
import org.elasticsearch.common.io.stream.Writeable;
import org.elasticsearch.common.xcontent.ObjectParser;
import org.elasticsearch.common.xcontent.ObjectParser.ValueType;
import org.elasticsearch.common.xcontent.ToXContent;
import org.elasticsearch.common.xcontent.XContentBuilder;
import org.elasticsearch.common.xcontent.XContentFactory;
import org.elasticsearch.common.xcontent.XContentParser;
import org.elasticsearch.common.xcontent.XContentParser.Token;
import org.elasticsearch.common.xcontent.XContentType;
import org.elasticsearch.index.query.QueryParseContext;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;

/**
 * Script represents used-defined input that can be used to
 * compile and execute a script from the {@link ScriptService}
 * based on the {@link ScriptType}.
 */
public final class Script implements ToXContent, Writeable {

    /**
     * The name of the of the default scripting language.
     */
    public static final String DEFAULT_SCRIPT_LANG = "painless";

    /**
     * The name of the default template language.
     */
    public static final String DEFAULT_TEMPLATE_LANG = "mustache";

    /**
     * The default {@link ScriptType}.
     */
    public static final ScriptType DEFAULT_SCRIPT_TYPE = ScriptType.INLINE;

    /**
     * Compiler option for {@link XContentType} used for templates.
     */
    public static final String CONTENT_TYPE_OPTION = "content_type";

    /**
     * Standard {@link ParseField} for outer level of script queries.
     */
    public static final ParseField SCRIPT_PARSE_FIELD = new ParseField("script");

    /**
     * Standard {@link ParseField} for lang on the inner level.
     */
    public static final ParseField LANG_PARSE_FIELD = new ParseField("lang");

    /**
     * Standard {@link ParseField} for options on the inner level.
     */
    public static final ParseField OPTIONS_PARSE_FIELD = new ParseField("options");

    /**
     * Standard {@link ParseField} for params on the inner level.
     */
    public static final ParseField PARAMS_PARSE_FIELD = new ParseField("params");

    /**
     * Helper class used by {@link ObjectParser} to store mutable {@link Script} variables and then
     * construct an immutable {@link Script} object based on parsed XContent.
     */
    private static final class Builder {
        private ScriptType type;
        private String lang;
        private String idOrCode;
        private Map<String, String> options;
        private Map<String, Object> params;

        private Builder() {
            // This cannot default to an empty map because options are potentially added at multiple points.
            this.options = new HashMap<>();
            this.params = Collections.emptyMap();
        }

        /**
         * Since inline scripts can accept code rather than just an id, they must also be able
         * to handle template parsing, hence the need for custom parsing code.  Templates can
         * consist of either an {@link String} or a JSON object.  If a JSON object is discovered
         * then the content type option must also be saved as a compiler option.
         */
        private void setInline(XContentParser parser) {
            try {
                if (type != null) {
                    throwOnlyOneOfType();
                }

                type = ScriptType.INLINE;

                if (parser.currentToken() == Token.START_OBJECT) {
                    XContentBuilder builder = XContentFactory.contentBuilder(parser.contentType());
                    idOrCode = builder.copyCurrentStructure(parser).bytes().utf8ToString();
                    options.put(CONTENT_TYPE_OPTION, parser.contentType().mediaType());
                } else {
                    idOrCode = parser.text();
                }
            } catch (IOException exception) {
                throw new UncheckedIOException(exception);
            }
        }

        /**
         * Set both the id and the type of the stored script.
         */
        private void setStored(String idOrCode) {
            if (type != null) {
                throwOnlyOneOfType();
            }

            type = ScriptType.STORED;
            this.idOrCode = idOrCode;
        }

        /**
         * Set both the id and the type of the file script.
         */
        private void setFile(String idOrCode) {
            if (type != null) {
                throwOnlyOneOfType();
            }

            type = ScriptType.FILE;
            this.idOrCode = idOrCode;
        }

        /**
         * Helper method to throw an exception if more than one type of {@link Script} is specified.
         */
        private void throwOnlyOneOfType() {
            throw new IllegalArgumentException("must only use one of [" +
                ScriptType.INLINE.getParseField().getPreferredName() + " + , " +
                ScriptType.STORED.getParseField().getPreferredName() + " + , " +
                ScriptType.FILE.getParseField().getPreferredName() + "]" +
                " when specifying a script");
        }

        private void setLang(String lang) {
            this.lang = lang;
        }

        /**
         * Options may have already been added if an inline template was specified.
         * Appends the user-defined compiler options with the internal compiler options.
         */
        private void setOptions(Map<String, String> options) {
            this.options.putAll(options);
        }

        private void setParams(Map<String, Object> params) {
            this.params = params;
        }

        /**
         * Validates the parameters and creates an {@link Script}.
         * @param defaultLang The default lang is not a compile-time constant and must be provided
         *                    at run-time this way in case a legacy default language is used from
         *                    previously stored queries.
         */
        private Script build(String defaultLang) {
            if (type == null) {
                throw new IllegalArgumentException(
                    "must specify either code for an [" + ScriptType.INLINE.getParseField().getPreferredName() + "] script " +
                        "or an id for a [" + ScriptType.STORED.getParseField().getPreferredName() + "] script " +
                        "or [" + ScriptType.FILE.getParseField().getPreferredName() + "] script");
            }

            if (idOrCode == null) {
                throw new IllegalArgumentException("must specify an id or code for a script");
            }

            if (options.size() > 1 || options.size() == 1 && options.get(CONTENT_TYPE_OPTION) == null) {
                throw new IllegalArgumentException("illegal compiler options [" + options + "] specified");
            }

            return new Script(type, this.lang == null ? defaultLang : this.lang, idOrCode, options, params);
        }
    }

    private static final ObjectParser<Builder, ParseFieldMatcherSupplier> PARSER = new ObjectParser<>("script", Builder::new);

    static {
        // Defines the fields necessary to parse a Script as XContent using an ObjectParser.
        PARSER.declareField(Builder::setInline, parser -> parser, ScriptType.INLINE.getParseField(), ValueType.OBJECT_OR_STRING);
        PARSER.declareString(Builder::setStored, ScriptType.STORED.getParseField());
        PARSER.declareString(Builder::setFile, ScriptType.FILE.getParseField());
        PARSER.declareString(Builder::setLang, LANG_PARSE_FIELD);
        PARSER.declareField(Builder::setOptions, XContentParser::mapStrings, OPTIONS_PARSE_FIELD, ValueType.OBJECT);
        PARSER.declareField(Builder::setParams, XContentParser::map, PARAMS_PARSE_FIELD, ValueType.OBJECT);
    }

    /**
     * Convenience method to call {@link Script#parse(XContentParser, ParseFieldMatcher, String)}
     * using the default scripting language.
     */
    public static Script parse(XContentParser parser, ParseFieldMatcher matcher) throws IOException {
        return parse(parser, matcher, DEFAULT_SCRIPT_LANG);
    }

    /**
     * Convenience method to call {@link Script#parse(XContentParser, ParseFieldMatcher, String)} using the
     * {@link ParseFieldMatcher} and scripting language provided by the {@link QueryParseContext}.
     */
    public static Script parse(XContentParser parser, QueryParseContext context) throws IOException {
        return parse(parser, context.getParseFieldMatcher(), context.getDefaultScriptLanguage());
    }

    /**
     * This will parse XContent into a {@link Script}.  The following formats can be parsed:
     *
     * The simple format defaults to an {@link ScriptType#INLINE} with no compiler options or user-defined params:
     *
     * Example:
     * {@code
     * "return Math.log(doc.popularity) * 100;"
     * }
     *
     * The complex format where {@link ScriptType} and idOrCode are required while lang, options and params are not required.
     *
     * {@code
     * {
     *     "<type (inline, stored, file)>" : "<idOrCode>",
     *     "lang" : "<lang>",
     *     "options" : {
     *         "option0" : "<option0>",
     *         "option1" : "<option1>",
     *         ...
     *     },
     *     "params" : {
     *         "param0" : "<param0>",
     *         "param1" : "<param1>",
     *         ...
     *     }
     * }
     * }
     *
     * Example:
     * {@code
     * {
     *     "inline" : "return Math.log(doc.popularity) * params.multiplier",
     *     "lang" : "painless",
     *     "params" : {
     *         "multiplier" : 100.0
     *     }
     * }
     * }
     *
     * This also handles templates in a special way.  If a complexly formatted query is specified as another complex
     * JSON object the query is assumed to be a template, and the format will be preserved.
     *
     * {@code
     * {
     *     "inline" : { "query" : ... },
     *     "lang" : "<lang>",
     *     "options" : {
     *         "option0" : "<option0>",
     *         "option1" : "<option1>",
     *         ...
     *     },
     *     "params" : {
     *         "param0" : "<param0>",
     *         "param1" : "<param1>",
     *         ...
     *     }
     * }
     * }
     *
     * @param parser       The {@link XContentParser} to be used.
     * @param matcher      The {@link ParseFieldMatcher} to be used.
     * @param defaultLang  The default language to use if no language is specified.  The default language isn't necessarily
     *                     the one defined by {@link Script#DEFAULT_SCRIPT_LANG} due to backwards compatiblity requirements
     *                     related to stored queries using previously default languauges.
     * @return             The parsed {@link Script}.
     */
    public static Script parse(XContentParser parser, ParseFieldMatcher matcher, String defaultLang) throws IOException {
        Objects.requireNonNull(defaultLang);

        Token token = parser.currentToken();

        if (token == null) {
            token = parser.nextToken();
        }

        if (token == Token.VALUE_STRING) {
            return new Script(ScriptType.INLINE, defaultLang, parser.text(), Collections.emptyMap());
        }

        return PARSER.apply(parser, () -> matcher).build(defaultLang);
    }

    private final ScriptType type;
    private final String lang;
    private final String idOrCode;
    private final Map<String, String> options;
    private final Map<String, Object> params;

    /**
     * Constructor for simple script using the default language and default type.
     * @param idOrCode The id or code to use dependent on the default script type.
     */
    public Script(String idOrCode) {
        this(DEFAULT_SCRIPT_TYPE, DEFAULT_SCRIPT_LANG, idOrCode, Collections.emptyMap(), Collections.emptyMap());
    }

    /**
     * Constructor for a script that does not need to use compiler options.
     * @param type     The {@link ScriptType}.
     * @param lang     The lang for this {@link Script}.
     * @param idOrCode The id for this {@link Script} if the {@link ScriptType} is {@link ScriptType#FILE} or {@link ScriptType#STORED}.
     *                 The code for this {@link Script} if the {@link ScriptType} is {@link ScriptType#INLINE}.
     * @param params   The user-defined params to be bound for script execution.
     */
    public Script(ScriptType type, String lang, String idOrCode, Map<String, Object> params) {
        this(type, lang, idOrCode, Collections.emptyMap(), params);
    }

    /**
     * Constructor for a script that requires the use of compiler options.
     * @param type     The {@link ScriptType}.
     * @param lang     The lang for this {@link Script}.
     * @param idOrCode The id for this {@link Script} if the {@link ScriptType} is {@link ScriptType#FILE} or {@link ScriptType#STORED}.
     *                 The code for this {@link Script} if the {@link ScriptType} is {@link ScriptType#INLINE}.
     * @param options  The options to be passed to the compiler for use at compile-time.
     * @param params   The user-defined params to be bound for script execution.
     */
    public Script(ScriptType type, String lang, String idOrCode, Map<String, String> options, Map<String, Object> params) {
        this.idOrCode = Objects.requireNonNull(idOrCode);
        this.type = Objects.requireNonNull(type);
        this.lang = Objects.requireNonNull(lang);
        this.options = Collections.unmodifiableMap(Objects.requireNonNull(options));
        this.params = Collections.unmodifiableMap(Objects.requireNonNull(params));

        if (type != ScriptType.INLINE && !options.isEmpty()) {
            throw new IllegalArgumentException(
                "Compiler options [" + options + "] cannot be specified at runtime for [" + type + "] scripts.");
        }
    }

    /**
     * Creates a {@link Script} read from an input stream.
     */
    public Script(StreamInput in) throws IOException {
        // Version 5.1+ requires all Script members to be non-null and supports the potential
        // for more options than just XContentType.  Reorders the read in contents to be in
        // same order as the constructor.
        if (in.getVersion().onOrAfter(Version.V_5_1_1_UNRELEASED)) {
            this.type = ScriptType.readFrom(in);
            this.lang = in.readString();
            this.idOrCode = in.readString();
            @SuppressWarnings("unchecked")
            Map<String, String> options = (Map<String, String>)(Map)in.readMap();
            this.options = options;
            this.params = in.readMap();
            // Prior to version 5.1 the script members are read in certain cases as optional and given
            // default values when necessary.  Also the only option supported is for XContentType.
        } else {
            String idOrCode = in.readString();
            ScriptType type;

            if (in.readBoolean()) {
                type = ScriptType.readFrom(in);
            } else {
                type = DEFAULT_SCRIPT_TYPE;
            }

            String lang = in.readOptionalString();

            if (lang == null) {
                lang = DEFAULT_SCRIPT_LANG;
            }

            Map<String, Object> params = in.readMap();

            if (params == null) {
                params = new HashMap<>();
            }

            Map<String, String> options = new HashMap<>();

            if (in.readBoolean()) {
                XContentType contentType = XContentType.readFrom(in);
                options.put(CONTENT_TYPE_OPTION, contentType.mediaType());
            }

            this.type = type;
            this.lang = lang;
            this.idOrCode = idOrCode;
            this.options = options;
            this.params = params;
        }
    }

    @Override
    public void writeTo(StreamOutput out) throws IOException {
        // Version 5.1+ requires all Script members to be non-null and supports the potential
        // for more options than just XContentType.  Reorders the written out contents to be in
        // same order as the constructor.
        if (out.getVersion().onOrAfter(Version.V_5_1_1_UNRELEASED)) {
            type.writeTo(out);
            out.writeString(lang);
            out.writeString(idOrCode);
            @SuppressWarnings("unchecked")
            Map<String, Object> options = (Map<String, Object>)(Map)this.options;
            out.writeMap(options);
            out.writeMap(params);
            // Prior to version 5.1 the Script members were possibly written as optional or null, though this is no longer
            // necessary since Script members cannot be null anymore, and there is no case where a null value wasn't equivalent
            // to it's default value when actually compiling/executing a script.  Meaning, there are no backwards compatibility issues,
            // and now there's enforced consistency.  Also the only supported compiler option was XContentType.
        } else {
            out.writeString(idOrCode);
            out.writeBoolean(true);
            type.writeTo(out);
            out.writeBoolean(true);
            out.writeString(lang);
            out.writeMap(params.isEmpty() ? null : params);

            if (options.containsKey(CONTENT_TYPE_OPTION)) {
                XContentType contentType = XContentType.fromMediaTypeOrFormat(options.get(CONTENT_TYPE_OPTION));
                out.writeBoolean(true);
                contentType.writeTo(out);
            } else {
                out.writeBoolean(false);
            }
        }
    }

    /**
     * This will build scripts into the following XContent structure:
     *
     * {@code
     * {
     *     "<type (inline, stored, file)>" : "<idOrCode>",
     *     "lang" : "<lang>",
     *     "options" : {
     *         "option0" : "<option0>",
     *         "option1" : "<option1>",
     *         ...
     *     },
     *     "params" : {
     *         "param0" : "<param0>",
     *         "param1" : "<param1>",
     *         ...
     *     }
     * }
     * }
     *
     * Example:
     * {@code
     * {
     *     "inline" : "return Math.log(doc.popularity) * params.multiplier;",
     *     "lang" : "painless",
     *     "params" : {
     *         "multiplier" : 100.0
     *     }
     * }
     * }
     *
     * Note that options and params will only be included if there have been any specified.
     *
     * This also handles templates in a special way.  If the {@link Script#CONTENT_TYPE_OPTION} option
     * is provided and the {@link ScriptType#INLINE} is specified then the template will be preserved as a raw field.
     *
     * {@code
     * {
     *     "inline" : { "query" : ... },
     *     "lang" : "<lang>",
     *     "options" : {
     *         "option0" : "<option0>",
     *         "option1" : "<option1>",
     *         ...
     *     },
     *     "params" : {
     *         "param0" : "<param0>",
     *         "param1" : "<param1>",
     *         ...
     *     }
     * }
     * }
     */
    @Override
    public XContentBuilder toXContent(XContentBuilder builder, Params builderParams) throws IOException {
        builder.startObject();

        String contentType = options.get(CONTENT_TYPE_OPTION);

        if (type == ScriptType.INLINE && contentType != null && builder.contentType().mediaType().equals(contentType)) {
            builder.rawField(type.getParseField().getPreferredName(), new BytesArray(idOrCode));
        } else {
            builder.field(type.getParseField().getPreferredName(), idOrCode);
        }

        builder.field(LANG_PARSE_FIELD.getPreferredName(), lang);

        if (!options.isEmpty()) {
            builder.field(OPTIONS_PARSE_FIELD.getPreferredName(), options);
        }

        if (!params.isEmpty()) {
            builder.field(PARAMS_PARSE_FIELD.getPreferredName(), params);
        }

        builder.endObject();

        return builder;
    }

    /**
     * @return The id for this {@link Script} if the {@link ScriptType} is {@link ScriptType#FILE} or {@link ScriptType#STORED}.
     *         The code for this {@link Script} if the {@link ScriptType} is {@link ScriptType#INLINE}.
     */
    public String getIdOrCode() {
        return idOrCode;
    }

    /**
     * @return The {@link ScriptType} for this {@link Script}.
     */
    public ScriptType getType() {
        return type;
    }

    /**
     * @return The language for this {@link Script}.
     */
    public String getLang() {
        return lang;
    }

    /**
     * @return The map of compiler options for this {@link Script}.
     */
    public Map<String, String> getOptions() {
        return options;
    }

    /**
     * @return The map of user-defined params for this {@link Script}.
     */
    public Map<String, Object> getParams() {
        return params;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;

        Script script = (Script)o;

        if (type != script.type) return false;
        if (!lang.equals(script.lang)) return false;
        if (!idOrCode.equals(script.idOrCode)) return false;
        if (!options.equals(script.options)) return false;
        return params.equals(script.params);

    }

    @Override
    public int hashCode() {
        int result = type.hashCode();
        result = 31 * result + lang.hashCode();
        result = 31 * result + idOrCode.hashCode();
        result = 31 * result + options.hashCode();
        result = 31 * result + params.hashCode();
        return result;
    }

    @Override
    public String toString() {
        return "Script{" +
            "type=" + type +
            ", lang='" + lang + '\'' +
            ", idOrCode='" + idOrCode + '\'' +
            ", options=" + options +
            ", params=" + params +
            '}';
    }
}
