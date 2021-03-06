/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.xpack.security.authc.support;

import org.elasticsearch.common.util.concurrent.ThreadContext;
import org.elasticsearch.xpack.security.authc.AuthenticationToken;
import org.elasticsearch.xpack.security.authc.Realm;
import org.elasticsearch.xpack.security.authc.RealmConfig;
import org.elasticsearch.xpack.security.user.User;

abstract class UsernamePasswordRealm extends Realm {

    UsernamePasswordRealm(String type, RealmConfig config) {
        super(type, config);
    }

    @Override
    public UsernamePasswordToken token(ThreadContext threadContext) {
        return UsernamePasswordToken.extractToken(threadContext);
    }

    public boolean supports(AuthenticationToken token) {
        return token instanceof UsernamePasswordToken;
    }
}
