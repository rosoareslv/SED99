/*
 * Copyright Elasticsearch B.V. and/or licensed to Elasticsearch B.V. under one
 * or more contributor license agreements. Licensed under the Elastic License;
 * you may not use this file except in compliance with the Elastic License.
 */
package org.elasticsearch.shield.authz.store;

import org.elasticsearch.shield.authz.permission.Role;

/**
 * An interface for looking up a role given a string role name
 */
public interface RolesStore {

    Role role(String role);

}
