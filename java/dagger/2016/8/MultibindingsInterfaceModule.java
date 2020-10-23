/*
 * Copyright (C) 2016 The Dagger Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package test;

import dagger.Module;
import dagger.Multibindings;
import java.util.Map;
import java.util.Set;
import javax.inject.Named;

/**
 * A module that uses a {@link Multibindings @Multibindings}-annotated nested interface to declare
 * multibindings.
 */
@Module
final class MultibindingsInterfaceModule {

  interface EmptiesSupertype {
    Set<Object> emptySet();

    Map<String, Object> emptyMap();

    Set<CharSequence> set();

    Map<String, CharSequence> map();
  }

  @Multibindings
  interface Empties extends EmptiesSupertype {
    @Named("complexQualifier")
    Set<Object> emptyQualifiedSet();

    @Named("complexQualifier")
    Map<String, Object> emptyQualifiedMap();

    @Named("complexQualifier")
    Set<CharSequence> qualifiedSet();

    @Named("complexQualifier")
    Map<String, CharSequence> qualifiedMap();
  }
}
