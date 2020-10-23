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

import org.apache.lucene.index.LeafReaderContext;
import org.elasticsearch.common.Nullable;
import org.elasticsearch.common.component.AbstractComponent;
import org.elasticsearch.common.settings.Settings;
import org.elasticsearch.search.lookup.SearchLookup;

import java.io.IOException;
import java.util.Map;

import static java.util.Collections.unmodifiableMap;

/**
 * A native script engine service.
 */
public class NativeScriptEngineService extends AbstractComponent implements ScriptEngineService {

    public static final String NAME = "native";

    private final Map<String, NativeScriptFactory> scripts;

    public NativeScriptEngineService(Settings settings, Map<String, NativeScriptFactory> scripts) {
        super(settings);
        this.scripts = unmodifiableMap(scripts);
    }

    @Override
    public String getType() {
        return NAME;
    }

    @Override
    public String getExtension() {
        return ""; // Native scripts have no extensions
    }

    @Override
    public Object compile(String scriptName, String scriptSource, Map<String, String> params) {
        NativeScriptFactory scriptFactory = scripts.get(scriptSource);
        if (scriptFactory != null) {
            return scriptFactory;
        }
        throw new IllegalArgumentException("Native script [" + scriptSource + "] not found");
    }

    @Override
    public ExecutableScript executable(CompiledScript compiledScript, @Nullable Map<String, Object> vars) {
        NativeScriptFactory scriptFactory = (NativeScriptFactory) compiledScript.compiled();
        return scriptFactory.newScript(vars);
    }

    @Override
    public SearchScript search(CompiledScript compiledScript, final SearchLookup lookup, @Nullable final Map<String, Object> vars) {
        final NativeScriptFactory scriptFactory = (NativeScriptFactory) compiledScript.compiled();
        final AbstractSearchScript script = (AbstractSearchScript) scriptFactory.newScript(vars);
        return new SearchScript() {
            @Override
            public LeafSearchScript getLeafSearchScript(LeafReaderContext context) throws IOException {
                script.setLookup(lookup.getLeafSearchLookup(context));
                return script;
            }
            @Override
            public boolean needsScores() {
                return scriptFactory.needsScores();
            }
        };
    }

    @Override
    public void close() {
    }

    @Override
    public boolean isInlineScriptEnabled() {
        return true;
    }
}
